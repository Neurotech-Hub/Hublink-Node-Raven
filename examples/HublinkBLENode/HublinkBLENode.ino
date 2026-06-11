// HublinkBLENode — Raven hardware + SD, Hublink BLE gateway, 10s NimBLE scan, daily RTC CSV logs.
//
// - Bring-up matches HubWheelHublink: beginHardware/I2C → DataLoggerHelper::begin → Raven SD mount
//   check → hublink.begin(advName) (Hublink then sees SD already initialized). When RTC is valid,
//   setup also creates today's JXV/JXS/JXB files with CSV headers so loop can append without races.
// - BLE name: JX_BBB + last three hex digits of BT MAC (uppercase), passed to hublink.begin(advName).
// - Status: green LED only — solid ON for all of setup after hardware init; one quick off→on pulse
//   before hublink.begin (advertising); two short flashes before each BLE scan; green off when idle;
//   slow ~250 ms toggle while blocked in the SD recovery wait loop after a runtime write failure.
// - Loop: hublink.sync(), BLE scan window, vitals/JXS/JXB when RTC valid, delay.
// - SD removal: any runtime appendLine failure surfaces a Serial line and enters a blocking wait
//   loop that re-mounts the card (end + begin) on a backoff while blinking green; loop resumes only
//   after a successful remount.
// - Gateway JSON timestamps (Hublink) update the DS3231 via setTimestampCallback, same as HubWheelHublink.
// - JXB scan: only peers whose advertised **name** starts with `JX_`; `peer_id` is that name (not MAC).
//
// Requires Neurotech-Hub Hublink library (NimBLE). Board: Tools → Bluetooth → NimBLE.

#include <Hublink.h>
#include <HublinkNodeRaven.h>
#include <cstdarg>
#include <cstdio>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
/// Half-period of the green-LED toggle while blocked waiting for SD remount.
static constexpr uint32_t kSdRecoveryBlinkMs = 250;
/// How often the SD recovery wait loop attempts a remount (independent of the LED toggle cadence).
static constexpr uint32_t kSdRecoveryRetryMs = 2000;

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

/// Fully tear down and re-mount the Raven SD service. Returns true only if a real card is present.
static bool tryRemountSd()
{
  node.sd().end();
  if (!node.sd().begin())
  {
    return false;
  }
  return node.sd().cardType() != CARD_NONE;
}

/// Block until SD is mounted again, blinking green at ~250 ms half-period and retrying the
/// underlying remount every kSdRecoveryRetryMs. Used after a runtime appendLine failure.
static void waitForSdReady()
{
  dbgln(F("[sd] entering recovery wait — blink green until SD remounts."));
  uint32_t lastRetryMs = 0;
  bool ledOn = false;
  while (true)
  {
    ledOn = !ledOn;
    digitalWrite(raven::PIN_LED_GREEN, ledOn ? HIGH : LOW);
    delay(kSdRecoveryBlinkMs);

    const uint32_t now = millis();
    if (now - lastRetryMs >= kSdRecoveryRetryMs)
    {
      lastRetryMs = now;
      if (tryRemountSd())
      {
        ledGreenOff();
        dbgln(F("[sd] remount ok; resuming."));
        return;
      }
      dbgln(F("[sd] remount attempt failed; still waiting."));
    }
  }
}

/// Append `line` to `path`. On failure, surface a Serial diagnostic, enter the SD recovery wait
/// loop, then retry the append once. `what` is a short F() label used in the Serial output.
static raven::ServiceStatus sdAppendOrRecover(const char *path, const String &line,
                                              const __FlashStringHelper *what)
{
  raven::ServiceStatus rc = node.sd().appendLine(path, line);
  if (rc == raven::ServiceStatus::Ok)
  {
    return rc;
  }
  Serial.print(F("[sd] write failed for "));
  Serial.print(what);
  Serial.print(F(" path="));
  Serial.println(path);
  Serial.flush();

  waitForSdReady();

  rc = node.sd().appendLine(path, line);
  if (rc != raven::ServiceStatus::Ok)
  {
    Serial.print(F("[sd] retry write still failed for "));
    Serial.println(what);
    Serial.flush();
  }
  return rc;
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
  raven::ServiceStatus rc = node.sd().appendLine(path, header);
  if (rc == raven::ServiceStatus::Ok)
  {
    return;
  }
  Serial.println(F("[sd] write failed for JXV header; entering recovery."));
  Serial.flush();
  waitForSdReady();
  if (!node.sd().exists(path))
  {
    (void)node.sd().appendLine(path, header);
  }
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
  const String header = String(F("unix,observer_id,peer_id,rssi"));
  raven::ServiceStatus rc = node.sd().appendLine(path, header);
  if (rc == raven::ServiceStatus::Ok)
  {
    return;
  }
  Serial.println(F("[sd] write failed for JXB header; entering recovery."));
  Serial.flush();
  waitForSdReady();
  if (!node.sd().exists(path))
  {
    (void)node.sd().appendLine(path, header);
  }
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
    if (sdAppendOrRecover(path, line, F("JXB row")) != raven::ServiceStatus::Ok)
    {
      // Recovery already attempted inside the wrapper; bail to avoid a long fail loop here.
      break;
    }
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

  if (sdAppendOrRecover(path, String(kJsvHeader), F("JXS header")) != raven::ServiceStatus::Ok)
  {
    return;
  }

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
  (void)sdAppendOrRecover(path, row, F("JXS row"));
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
    if (sdAppendOrRecover(path, header, F("JXV header")) != raven::ServiceStatus::Ok)
    {
      return;
    }
  }

  raven::SampleFields sample = logger.captureSample();
  const String row = raven::DataLoggerHelper::toCsv(sample, kCsvFieldMask);
  if (sdAppendOrRecover(path, row, F("JXV row")) != raven::ServiceStatus::Ok)
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

  // Free heap / min-ever heap / largest contiguous block / current task stack high-water mark.
  // ESP.getMinFreeHeap() and uxTaskGetStackHighWaterMark report the worst-case low water marks
  // since boot, which is more useful for spotting leaks/overflow than the instantaneous values.
  dbgf("[loop %lu] mem heap_free=%lu min_free=%lu max_alloc=%lu stack_min_free=%lu",
       static_cast<unsigned long>(gLoopCount),
       static_cast<unsigned long>(ESP.getFreeHeap()),
       static_cast<unsigned long>(ESP.getMinFreeHeap()),
       static_cast<unsigned long>(ESP.getMaxAllocHeap()),
       static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));

  dbgf("[loop %lu] delay(%lums)...", static_cast<unsigned long>(gLoopCount),
       static_cast<unsigned long>(kLoopDelayMs));
  delay(kLoopDelayMs);
}
