// HublinkBLENode — Raven hardware + SD, Hublink BLE gateway, 10s NimBLE scan, daily RTC CSV logs.
//
// - BLE name: JX_BBB + last three hex digits of BT MAC (uppercase), passed to hublink.begin(advName).
// - Loop: hublink.sync() (blocking), blocking NimBLE scan window (10s), vitals/settings/JBV logging, delay.
// - CSV files (RTC date, no underscore before date): /JXVyyyymmdd.csv, /JSVyyyymmdd.csv, /JBVyyyymmdd.csv
//   SD writes only when RTC read is Ok and DateTime is valid (no fallback clock for filenames).
//
// Requires Neurotech-Hub Hublink library (NimBLE). Board: Tools → Bluetooth → NimBLE.

#include <Hublink.h>
#include <HublinkNodeRaven.h>
#include <cstdio>
#include <esp_mac.h>
#include <map>
#include <string>

static constexpr uint32_t kLoopDelayMs = 2000;
static constexpr uint32_t kScanWindowMs = 10000;
static constexpr uint32_t kVitalsIntervalMs = 60000;
static constexpr uint32_t kScanIntervalS = kScanWindowMs / 1000;
static constexpr uint32_t kVitalsIntervalS = kVitalsIntervalMs / 1000;

/// Raven library / sketch firmware string for JSV (align with library.properties when you bump releases).
static constexpr char kFwVersion[] = "0.2.1";

static constexpr raven::CsvFieldMask kCsvFieldMask = raven::csvFields({
    raven::CsvField::Millis,
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

namespace {

std::map<std::string, int> gPeerMaxRssi;

class HubScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
  {
    if (!advertisedDevice)
    {
      return;
    }
    const std::string id = advertisedDevice->getAddress().toString();
    const int rssi = advertisedDevice->getRSSI();
    const auto it = gPeerMaxRssi.find(id);
    if (it == gPeerMaxRssi.end() || rssi > it->second)
    {
      gPeerMaxRssi[id] = rssi;
    }
  }
};

HubScanCallbacks gHubScanCallbacks;

} // namespace

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

static void runBleScanWindowAndLogJbv()
{
  gPeerMaxRssi.clear();

  if (!NimBLEDevice::init("JX_scan"))
  {
    Serial.println(F("NimBLEDevice::init failed (scan)."));
    return;
  }

  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&gHubScanCallbacks, false);
  pScan->setActiveScan(true);
  if (!pScan->start(kScanWindowMs, false, true))
  {
    Serial.println(F("BLE scan start failed."));
  }

  (void)NimBLEDevice::deinit(true);

  raven::RtcReading rtc;
  if (!rtcOkForLogging(rtc) || gPeerMaxRssi.empty())
  {
    return;
  }

  char path[28];
  buildDailyPath("JBV", rtc.now, path, sizeof(path));

  const bool newFile = !node.sd().exists(path);
  if (newFile)
  {
    (void)node.sd().appendLine(path, String(F("unix,observer_id,peer_id,rssi")));
  }

  const uint32_t unixTs = rtc.now.unixtime();
  for (const auto &kv : gPeerMaxRssi)
  {
    String line;
    line.reserve(64);
    line += unixTs;
    line += ',';
    line += gAdvName;
    line += ',';
    line += kv.first.c_str();
    line += ',';
    line += kv.second;
    (void)node.sd().appendLine(path, line);
  }
}

static void ensureJsvForNewDay(const raven::RtcReading &rtc)
{
  char path[28];
  buildDailyPath("JSV", rtc.now, path, sizeof(path));
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

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("HublinkBLENode: Serial ready"));

  node.beginHardware();
  node.beginI2C();

  if (!node.sd().begin() || node.sd().cardType() == CARD_NONE)
  {
    Serial.println(F("SD mount failed. Halting."));
    while (true)
    {
      delay(1000);
    }
  }

  if (!node.rtc().begin())
  {
    Serial.println(F("RTC begin failed; SD vitals/JBV/JSV logging will be skipped until RTC is valid."));
  }

  if (!logger.begin())
  {
    Serial.println(F("DataLoggerHelper::begin failed."));
  }

  gAdvName = buildAdvName();
  Serial.print(F("advName="));
  Serial.println(gAdvName);

  if (!hublink.begin(gAdvName))
  {
    Serial.println(F("Hublink begin failed. Halting."));
    while (true)
    {
      delay(1000);
    }
  }
  hublink.watchdogTimeoutMs = 60000;

  gLastVitalsMs = millis();

  Serial.println(F("Hublink ready."));
}

void loop()
{
  (void)hublink.sync();

  runBleScanWindowAndLogJbv();

  raven::RtcReading rtc;
  if (rtcOkForLogging(rtc))
  {
    ensureJsvForNewDay(rtc);
    maybeAppendVitals(rtc);
  }

  delay(kLoopDelayMs);
}
