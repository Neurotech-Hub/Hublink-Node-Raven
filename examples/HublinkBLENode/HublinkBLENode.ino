// HublinkBLENode — Raven hardware + SD, Hublink BLE gateway, 10s NimBLE scan, daily RTC CSV logs.
//
// - Bring-up matches HubWheelHublink: beginHardware/I2C → DataLoggerHelper::begin → Raven SD mount
//   check → hublink.begin(advName) (Hublink then sees SD already initialized). When RTC is valid,
//   setup also creates today's JXV/JXS/JXB files with CSV headers so loop can append without races.
// - BLE name: JX_BBB + last three hex digits of BT MAC (uppercase), passed to hublink.begin(advName).
// - Status: green LED only — solid ON for all of setup after hardware init; one quick off→on pulse
//   before hublink.begin (advertising); two short flashes before each BLE scan; green off when idle.
// - Loop: hublink.sync(), BLE scan window, vitals/JXS/JXB when RTC valid, delay.
// - Gateway JSON timestamps (Hublink) update the DS3231 via setTimestampCallback, same as HubWheelHublink.
// - JXB scan: only peers whose advertised **name** starts with `JX_`; `peer_id` is that name (not MAC).
//
// Requires Neurotech-Hub Hublink library (NimBLE). Board: Tools → Bluetooth → NimBLE.

#include <Hublink.h>
#include <HublinkNodeRaven.h>
#include <cstdarg>
#include <cstdio>
#include <esp_mac.h>
#include <map>
#include <string>

static constexpr uint32_t kLoopDelayMs = 2000;
static constexpr uint32_t kScanWindowMs = 10000;
/// Let controller/host settle after Hublink may have called NimBLE deinit (Hublink uses multi-step delays).
static constexpr uint32_t kAfterHublinkBleCoolMs = 200;
/// After a forced deinit when NimBLE still reports initialized (partial/stale state).
static constexpr uint32_t kNimbleForceDeinitSettleMs = 200;
/// After NimBLEDevice::init before getScan/getResults.
static constexpr uint32_t kNimblePostInitSettleMs = 250;
static constexpr uint32_t kVitalsIntervalMs = 60000;
static constexpr uint32_t kScanIntervalS = kScanWindowMs / 1000;
static constexpr uint32_t kVitalsIntervalS = kVitalsIntervalMs / 1000;
/// Green-only status blinks (short, low duty).
static constexpr uint32_t kLedBlinkMs = 70;

/// Raven library / sketch firmware string for JXS settings row (align with library.properties when you bump releases).
static constexpr char kFwVersion[] = "0.2.1";

static constexpr raven::CsvFieldMask kCsvFieldMask = raven::csvFields({
    raven::CsvField::RtcUnix,
    raven::CsvField::DateTime,
    raven::CsvField::BattV,
    raven::CsvField::BattPer,
    raven::CsvField::Lux,
    raven::CsvField::TempC,
    raven::CsvField::HumidityPct,
    raven::CsvField::GasKOhm,
});

static String compactBtMacHex()
{
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char full[13];
  snprintf(full, sizeof(full), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(full);
}

static String buildAdvName()
{
  const String compact = compactBtMacHex();
  const String tail =
      compact.length() >= 3 ? compact.substring(compact.length() - 3) : compact;
  String name = String("JX_BBB") + tail;
  name.toUpperCase();
  return name;
}

raven::HublinkNode node;
raven::DataLoggerHelper logger(node);
Hublink hublink(raven::PIN_SD_CS);
static String gAdvName;
static uint32_t gLastVitalsMs = 0;
static uint32_t gLoopCount = 0;

static void dbgln(const __FlashStringHelper *msg)
{
  Serial.println(msg);
  Serial.flush();
}

static void ledGreenOn()
{
  digitalWrite(raven::PIN_LED_GREEN, HIGH);
}

static void ledGreenOff()
{
  digitalWrite(raven::PIN_LED_GREEN, LOW);
}

/// One quick dip (off→on) while LED was on for setup; ends with green on.
static void ledGreenBlinkOnceBeforeAdvertising()
{
  ledGreenOff();
  delay(kLedBlinkMs);
  ledGreenOn();
  delay(kLedBlinkMs);
}

/// Two brief flashes from dark (before each scan); ends with green off.
static void ledGreenBlinkTwiceBeforeScan()
{
  ledGreenOn();
  delay(kLedBlinkMs);
  ledGreenOff();
  delay(kLedBlinkMs);
  ledGreenOn();
  delay(kLedBlinkMs);
  ledGreenOff();
}

static void dbgf(const char *fmt, ...)
{
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
  Serial.flush();
}

static bool peerAdvNameIsJxFamily(const std::string &name)
{
  return name.size() >= 3 && name.compare(0, 3, "JX_") == 0;
}

/// Append a single CSV field; quote if needed for commas/quotes.
static void appendCsvField(String &line, const std::string &field)
{
  const bool mustQuote =
      field.find(',') != std::string::npos || field.find('"') != std::string::npos ||
      field.find('\n') != std::string::npos || field.find('\r') != std::string::npos;
  if (!mustQuote)
  {
    line += field.c_str();
    return;
  }
  line += '"';
  for (const char c : field)
  {
    if (c == '"')
    {
      line += "\"\"";
    }
    else
    {
      line += c;
    }
  }
  line += '"';
}

static bool rtcOkForLogging(raven::RtcReading &out)
{
  out = node.rtc().readSample();
  return out.status == raven::ServiceStatus::Ok && out.now.isValid();
}

/// /JXVyyyymmdd.csv style (prefix 3 letters + compact date).
static void buildDailyPath(const char *prefix3, const DateTime &dt, char *out, size_t outLen)
{
  snprintf(out, outLen, "/%s%04d%02d%02d.csv", prefix3, dt.year(), dt.month(), dt.day());
}

/// Create today's JXV file with a header row only if missing (does not touch gLastVitalsMs).
static void ensureJxvDailyHeaderOnly(const raven::RtcReading &rtc)
{
  char path[28];
  buildDailyPath("JXV", rtc.now, path, sizeof(path));
  if (node.sd().exists(path))
  {
    return;
  }
  const String header = raven::DataLoggerHelper::csvHeader(kCsvFieldMask);
  (void)node.sd().appendLine(path, header);
}

/// Create today's JXB file with a header row only if missing.
static void ensureJxbDailyHeaderOnly(const raven::RtcReading &rtc)
{
  char path[28];
  buildDailyPath("JXB", rtc.now, path, sizeof(path));
  if (node.sd().exists(path))
  {
    return;
  }
  (void)node.sd().appendLine(path, String(F("unix,observer_id,peer_id,rssi")));
}

static void runBleScanWindowAndLogJbv()
{
  // NimBLE init/deinit here runs only from loop() (Arduino main task), not from BLE callbacks.
  // Cool-down + isInitialized guard tolerate Hublink stopAdvertising/deinit timing vs our scan window.
  //
  // Max RSSI per advertised **name** (not MAC). Only names starting with "JX_" (active scan for scan response).
  std::map<std::string, int> peerNameMaxRssi;

  delay(kAfterHublinkBleCoolMs);

  if (NimBLEDevice::isInitialized())
  {
    dbgln(F("[scan] NimBLE still initialized; forcing deinit before scan init."));
    (void)NimBLEDevice::deinit(true);
    delay(kNimbleForceDeinitSettleMs);
  }

  dbgln(F("[scan] NimBLEDevice::init..."));
  if (!NimBLEDevice::init("JX_scan"))
  {
    dbgln(F("[scan] NimBLEDevice::init failed."));
    return;
  }
  delay(kNimblePostInitSettleMs);

  NimBLEScan *pScan = NimBLEDevice::getScan();
  if (pScan == nullptr)
  {
    dbgln(F("[scan] getScan() returned null."));
    (void)NimBLEDevice::deinit(true);
    return;
  }

  pScan->setActiveScan(true);
  dbgln(F("[scan] getResults (blocking ~10s)..."));
  const NimBLEScanResults results = pScan->getResults(kScanWindowMs, false);

  const int n = results.getCount();
  for (int i = 0; i < n; ++i)
  {
    const NimBLEAdvertisedDevice *dev = results.getDevice(static_cast<uint32_t>(i));
    if (dev == nullptr || !dev->haveName())
    {
      continue;
    }
    const std::string peerName = dev->getName();
    if (!peerAdvNameIsJxFamily(peerName))
    {
      continue;
    }
    const int rssi = dev->getRSSI();
    const auto it = peerNameMaxRssi.find(peerName);
    if (it == peerNameMaxRssi.end() || rssi > it->second)
    {
      peerNameMaxRssi[peerName] = rssi;
    }
  }

  (void)NimBLEDevice::deinit(true);
  dbgf("[scan] done; raw devices=%d JX_ peers(unique)=%u", n,
       static_cast<unsigned>(peerNameMaxRssi.size()));

  raven::RtcReading rtc;
  const bool rtcOk = rtcOkForLogging(rtc);
  if (!rtcOk)
  {
    dbgln(F("[scan] skip JXB: RTC not valid for logging."));
    return;
  }

  char path[28];
  buildDailyPath("JXB", rtc.now, path, sizeof(path));
  ensureJxbDailyHeaderOnly(rtc);

  if (peerNameMaxRssi.empty())
  {
    dbgln(F("[scan] skip JXB rows: no JX_ advertisement names this window."));
    return;
  }

  const uint32_t unixTs = rtc.now.unixtime();
  for (const auto &kv : peerNameMaxRssi)
  {
    String line;
    line.reserve(80);
    line += unixTs;
    line += ',';
    line += gAdvName;
    line += ',';
    appendCsvField(line, kv.first);
    line += ',';
    line += kv.second;
    (void)node.sd().appendLine(path, line);
  }
}

static void ensureJsvForNewDay(const raven::RtcReading &rtc)
{
  char path[28];
  buildDailyPath("JXS", rtc.now, path, sizeof(path));
  if (node.sd().exists(path))
  {
    return;
  }

  static constexpr char kJsvHeader[] =
      "fw_version,scan_interval_s,adv_interval_s,vitals_interval,ble_name";

  (void)node.sd().appendLine(path, String(kJsvHeader));

  // adv_interval_s = Hublink time between advertising/sync attempts (meta advertise_every).
  const uint32_t advEveryS = hublink.advertise_every;

  String row;
  row.reserve(128);
  row += kFwVersion;
  row += ',';
  row += kScanIntervalS;
  row += ',';
  row += advEveryS;
  row += ',';
  row += kVitalsIntervalS;
  row += ',';
  row += gAdvName;
  (void)node.sd().appendLine(path, row);
}

/// When RTC is valid, ensure today's daily CSV files exist with headers (JXS also gets its settings row).
static void ensureTodaysDailyCsvFiles(const raven::RtcReading &rtc)
{
  ensureJxvDailyHeaderOnly(rtc);
  ensureJxbDailyHeaderOnly(rtc);
  ensureJsvForNewDay(rtc);
}

static void maybeAppendVitals(const raven::RtcReading &rtc)
{
  const uint32_t nowMs = millis();
  if ((nowMs - gLastVitalsMs) < kVitalsIntervalMs)
  {
    return;
  }

  char path[28];
  buildDailyPath("JXV", rtc.now, path, sizeof(path));

  const bool newFile = !node.sd().exists(path);
  if (newFile)
  {
    const String header = raven::DataLoggerHelper::csvHeader(kCsvFieldMask);
    if (node.sd().appendLine(path, header) != raven::ServiceStatus::Ok)
    {
      return;
    }
  }

  raven::SampleFields sample = logger.captureSample();
  const String row = raven::DataLoggerHelper::toCsv(sample, kCsvFieldMask);
  if (node.sd().appendLine(path, row) != raven::ServiceStatus::Ok)
  {
    return;
  }
  gLastVitalsMs = nowMs;
}

static void onTimestampReceived(uint32_t timestamp)
{
  // Lets gateway-provided timestamps update on-device RTC.
  node.rtc().adjust(DateTime(timestamp));
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  dbgln(F("HublinkBLENode: Serial ready"));

  dbgln(F("[setup] beginHardware..."));
  node.beginHardware();
  dbgln(F("[setup] beginI2C..."));
  node.beginI2C();

  ledGreenOn();

  dbgln(F("[setup] DataLoggerHelper::begin..."));
  if (!logger.begin())
  {
    dbgln(F("[setup] DataLoggerHelper::begin failed."));
  }
  else
  {
    dbgln(F("[setup] DataLoggerHelper ok."));
  }

  dbgln(F("[setup] Raven SD mount..."));
  if (!node.sd().begin() || node.sd().cardType() == CARD_NONE)
  {
    dbgln(F("SD mount failed. Halting."));
    ledGreenOff();
    while (true)
    {
      delay(1000);
    }
  }
  dbgf("[setup] SD ok, cardType=%u", static_cast<unsigned>(node.sd().cardType()));

  gAdvName = buildAdvName();
  Serial.print(F("[setup] advName="));
  Serial.println(gAdvName);
  Serial.flush();

  ledGreenBlinkOnceBeforeAdvertising();
  dbgln(F("[setup] hublink.begin()..."));
  if (!hublink.begin(gAdvName))
  {
    dbgln(F("Hublink begin failed. Halting."));
    ledGreenOff();
    while (true)
    {
      delay(1000);
    }
  }
  dbgln(F("[setup] hublink.begin() returned true."));
  hublink.setTimestampCallback(onTimestampReceived);
  hublink.watchdogTimeoutMs = 60000;

  gLastVitalsMs = millis();

  {
    raven::RtcReading rtcBoot;
    if (rtcOkForLogging(rtcBoot))
    {
      dbgln(F("[setup] RTC ok — ensuring today's JXV/JXS/JXB CSV shells (headers)."));
      ensureTodaysDailyCsvFiles(rtcBoot);
    }
    else
    {
      dbgln(F("[setup] RTC not valid — skip pre-creating daily CSV files; loop will retry."));
    }
  }

  ledGreenOff();
  dbgln(F("Hublink ready — entering loop."));
}

void loop()
{
  ++gLoopCount;
  dbgf("[loop %lu] hublink.sync()...", static_cast<unsigned long>(gLoopCount));

  const bool syncOk = hublink.sync();
  dbgf("[loop %lu] sync returned %s", static_cast<unsigned long>(gLoopCount),
       syncOk ? "true" : "false");

  ledGreenBlinkTwiceBeforeScan();
  runBleScanWindowAndLogJbv();

  raven::RtcReading rtc;
  if (rtcOkForLogging(rtc))
  {
    dbgln(F("[loop] RTC ok — JXS/JXV may write."));
    ensureTodaysDailyCsvFiles(rtc);
    maybeAppendVitals(rtc);
  }
  else
  {
    dbgln(F("[loop] RTC not ok — skip CSV writes."));
  }

  dbgf("[loop %lu] delay(%lums)...", static_cast<unsigned long>(gLoopCount),
       static_cast<unsigned long>(kLoopDelayMs));
  delay(kLoopDelayMs);
}
