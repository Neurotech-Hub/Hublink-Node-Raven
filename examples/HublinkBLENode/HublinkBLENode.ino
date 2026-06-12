// HublinkBLENode — Raven hardware + SD, Hublink BLE gateway, NimBLE scan, daily RTC CSV logs,
// duty-cycled via deep sleep.
//
// - Wake → bring-up (beginHardware/I2C → DataLoggerHelper::begin → Raven SD mount check →
//   hublink.begin(advName)) → hublink.sync(kMasterIntervalSeconds) → NimBLE active scan for
//   kMasterIntervalSeconds → JXV/JXS/JXB CSV writes when RTC valid → deep sleep for
//   (kWakeCyclePeriodSeconds − measured wake elapsed). loop() is empty; setup() never returns.
// - Cadence: each wake aims for a fixed kWakeCyclePeriodSeconds period of *non-Hublink* work
//   from boot to next boot. enterDeepSleep computes (target − (millis() − gHublinkSyncTotalMs))
//   so a long gateway connection or repeated RTC-recovery syncs never compress the next sleep
//   window. The result is clamped to [kMinDeepSleepUs, kWakeCyclePeriodUs] so deep_sleep_start
//   never receives 0 or a runaway value. trackedHublinkSync() must be used everywhere instead
//   of hublink.sync() for the accounting to be correct.
// - BLE name: JX_BBB + last three hex digits of BT MAC (uppercase), passed to hublink.begin(advName).
// - Status: green LED only — solid ON for all of setup after hardware init; 3 quick 50 ms blinks
//   ending OFF just before hublink.sync (LED stays OFF during the sync window); 1 quick 50 ms
//   blink ending OFF just before each BLE scan (LED stays OFF during the scan window); green
//   solid ON during waitForRtcReady's sync windows; slow ~250 ms toggle while blocked in the
//   SD recovery wait loop on write failure; OFF during deep sleep.
// - Sleep current: enterDeepSleep calls node.setExternalRailsEnabled(false) so PIN_I2C_EN and
//   PIN_SD_EN both go HIGH (rails off) before esp_deep_sleep_start.
// - SD removal: any appendLine failure surfaces a Serial line and enters a blocking wait loop that
//   re-mounts the card (end + begin) on a backoff while blinking green; the wake resumes only after
//   a successful remount. Boot-time SD mount also routes through this path (no permanent halt).
// - RTC recovery: if the DS3231 has lost time (battery dead), the wake parks in a forever loop
//   that runs hublink.sync(kMasterIntervalSeconds) with green LED held ON, then delays
//   kRtcRecoveryRestMs between sync windows, until a gateway pushes a timestamp via
//   setTimestampCallback (which adjusts the DS3231).
// - Gateway JSON timestamps (Hublink) update the DS3231 via setTimestampCallback.
// - JXB scan: only peers whose advertised **name** starts with `JX_`; `peer_id` is that name (not MAC).
// - Hublink keeps NimBLE self-contained: it init's during hublink.begin() and deinit's after each
//   sync window. So this sketch must independently re-init NimBLE for its scan (NimBLEDevice::init
//   "JX_scan"), then deinit again before deep sleep. That means each wake performs TWO BT controller
//   bring-ups; the deep-sleep cadence keeps the per-hour count low so the rare ipc0 stack pressure
//   that previously crashed the continuous loop turns into "this wake reboots, next wake retries".
//
// Requires Neurotech-Hub Hublink library (NimBLE). Board: Tools → Bluetooth → NimBLE.

#include <Hublink.h>
#include <HublinkNodeRaven.h>
#include <cstdarg>
#include <cstdio>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <map>
#include <string>

/// Length (seconds) of the Hublink sync window, the NimBLE scan window, and the JXS interval
/// fields. Independent of the total wake cycle period.
static constexpr uint32_t kMasterIntervalSeconds = 10;
static constexpr uint32_t kScanWindowMs = kMasterIntervalSeconds * 1000UL;
/// Target wake-to-wake period (seconds). enterDeepSleep sleeps for the remainder of this budget
/// after subtracting the measured millis() elapsed since boot, so total cadence is approximately
/// constant regardless of how long sync/scan/SD work took.
static constexpr uint32_t kWakeCyclePeriodSeconds = 60;
static constexpr uint64_t kWakeCyclePeriodUs =
    static_cast<uint64_t>(kWakeCyclePeriodSeconds) * 1000000ULL;
/// Floor for the deep-sleep duration so a wake that overran the cycle (long recovery loops, etc.)
/// still actually deep-sleeps and resets the chip rather than falling through to a logical no-op.
static constexpr uint64_t kMinDeepSleepUs = 1000000ULL;
/// Hublink internal watchdog timeout (5 minutes) — kept generous since each wake only calls
/// hublink.sync() once with a short window.
static constexpr uint32_t kHublinkWatchdogMs = 300000;
/// Cool-down after Hublink finishes its sync window so the controller fully settles before we
/// take ownership for the scan. Hublink's stopAdvertising only waits ~170 ms internally after
/// NimBLEDevice::deinit before sync() returns; an earlier version of this sketch happened to add
/// ~280 ms of LED-blink delay between sync() and the scan, which empirically masked the BT
/// controller's residual teardown work and kept ipc0 stack pressure manageable. The current
/// LED choreography only adds ~100 ms of pre-scan blink, so this cool-down absorbs the slack
/// (and then some) to keep the effective settle above the previously-stable ~480 ms total.
static constexpr uint32_t kAfterHublinkBleCoolMs = 2000;
/// Settle delay if NimBLE still reports initialized when we expected it cleared.
static constexpr uint32_t kNimbleForceDeinitSettleMs = 500;
/// Settle delay between NimBLEDevice::init and the first getScan/getResults call.
static constexpr uint32_t kNimblePostInitSettleMs = 250;
/// Extra serial settle when USB is detected so an attached host's CDC port can enumerate.
static constexpr uint32_t kUsbSerialSettleMs = 2000;
/// Half-period of the green-LED quick blinks used as transition cues (3x before sync, 1x before scan).
static constexpr uint32_t kLedQuickBlinkMs = 50;
/// Half-period of the green-LED toggle while blocked waiting for SD remount.
static constexpr uint32_t kSdRecoveryBlinkMs = 250;
/// How often the SD recovery wait loop attempts a remount (independent of the LED toggle cadence).
static constexpr uint32_t kSdRecoveryRetryMs = 2000;
/// Quiet rest between hublink.sync() retries while waiting for the RTC to acquire a valid time.
static constexpr uint32_t kRtcRecoveryRestMs = 5000;

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
/// Total wall-clock millis spent inside hublink.sync() across this wake. Subtracted from the
/// cycle budget in enterDeepSleep so a gateway operator holding a long connection doesn't
/// compress the next sleep window. Re-zeroes naturally on the next wake (deep-sleep wake = chip reset).
static uint32_t gHublinkSyncTotalMs = 0;

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

/// Emit `n` discrete on/off blinks with `halfPeriodMs` per state. Always normalizes to LED OFF
/// first so the user sees `n` distinct pulses regardless of prior LED state, and ends with LED OFF.
static void ledGreenBlinkNTimes(uint8_t n, uint32_t halfPeriodMs)
{
  ledGreenOff();
  for (uint8_t i = 0; i < n; ++i)
  {
    delay(halfPeriodMs);
    ledGreenOn();
    delay(halfPeriodMs);
    ledGreenOff();
  }
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

/// Wrap hublink.sync() with wall-clock measurement so enterDeepSleep can subtract gateway-
/// connected time from the cycle budget. Use this for *every* sync call in this sketch
/// (main wake + RTC recovery loop) so the tracker accumulates correctly.
static bool trackedHublinkSync(uint32_t durationSeconds)
{
  const uint32_t t0 = millis();
  const bool ok = hublink.sync(durationSeconds);
  const uint32_t dt = millis() - t0;
  gHublinkSyncTotalMs += dt;
  return ok;
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

/// Create today's JXV file with a header row only if missing.
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

/// Active-scan for kScanWindowMs after Hublink's sync window has ended. Hublink owns its own
/// NimBLE lifecycle and deinits after each sync, so this sketch independently brings the stack
/// up for the scan and tears it down again before returning. Max RSSI per advertised **name**
/// (not MAC), only names starting with "JX_".
///
/// Stack notes: NimBLEDevice::init runs from setup() (Arduino main task) and dispatches BT
/// controller bring-up to ipc0. The default ipc0 stack is small; on Arduino-ESP32 there's no
/// portable knob to raise it, so the deep-sleep cadence is the primary mitigation — a rare
/// canary trip during init becomes "this wake reboots, the next wake retries" rather than a
/// long-running deployment hang.
static void runBleScanWindowAndLogJbv()
{
  std::map<std::string, int> peerNameMaxRssi;

  delay(kAfterHublinkBleCoolMs);

  if (NimBLEDevice::isInitialized())
  {
    dbgln(F("[scan] NimBLE still initialized after Hublink sync; forcing deinit before scan init."));
    (void)NimBLEDevice::deinit(true);
    delay(kNimbleForceDeinitSettleMs);
  }

  dbgln(F("[scan] NimBLEDevice::init(\"JX_scan\")..."));
  if (!NimBLEDevice::init("JX_scan"))
  {
    dbgln(F("[scan] NimBLEDevice::init failed; skipping scan."));
    return;
  }
  delay(kNimblePostInitSettleMs);

  NimBLEScan *pScan = NimBLEDevice::getScan();
  if (pScan == nullptr)
  {
    dbgln(F("[scan] getScan() returned null; deinit and skip."));
    (void)NimBLEDevice::deinit(true);
    return;
  }

  pScan->setActiveScan(true);
  dbgf("[scan] getResults (blocking ~%lus)...", static_cast<unsigned long>(kMasterIntervalSeconds));
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

  // Release the BLE stack before any SD work so a card-removed wait loop doesn't keep the
  // controller initialized longer than necessary.
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

  // All three intervals are driven by the single deep-sleep cadence in this design.
  String row;
  row.reserve(128);
  row += kFwVersion;
  row += ',';
  row += kMasterIntervalSeconds;
  row += ',';
  row += kMasterIntervalSeconds;
  row += ',';
  row += kMasterIntervalSeconds;
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

/// Capture sensors and append a single JXV vitals row. Headers are pre-created in
/// ensureTodaysDailyCsvFiles, but we still guard against a missing file (e.g. fresh card swapped in
/// during recovery) by writing a header on demand.
static void appendVitalsRow(const raven::RtcReading &rtc)
{
  char path[28];
  buildDailyPath("JXV", rtc.now, path, sizeof(path));

  if (!node.sd().exists(path))
  {
    const String header = raven::DataLoggerHelper::csvHeader(kCsvFieldMask);
    if (sdAppendOrRecover(path, header, F("JXV header")) != raven::ServiceStatus::Ok)
    {
      return;
    }
  }

  raven::SampleFields sample = logger.captureSample();
  const String row = raven::DataLoggerHelper::toCsv(sample, kCsvFieldMask);
  (void)sdAppendOrRecover(path, row, F("JXV row"));
}

static void onTimestampReceived(uint32_t timestamp)
{
  // Lets gateway-provided timestamps update on-device RTC.
  node.rtc().adjust(DateTime(timestamp));
}

/// Block until the DS3231 reports a valid date. The most likely cause of an invalid date is a
/// dead RTC backup battery, so we cannot make progress without a gateway-supplied timestamp.
/// Each iteration: hold green LED ON, run hublink.sync(kMasterIntervalSeconds) (during which
/// onTimestampReceived may fire and adjust the DS3231), then check the RTC; if still invalid,
/// LED off and rest for kRtcRecoveryRestMs before retrying. Caller is responsible for any LED
/// state it wants after this returns.
static void waitForRtcReady()
{
  raven::RtcReading probe;
  if (rtcOkForLogging(probe))
  {
    return;
  }
  dbgln(F("[rtc] DS3231 has no valid date (likely backup battery dead). Awaiting gateway timestamp."));
  while (true)
  {
    ledGreenOn();
    dbgf("[rtc] hublink.sync(%lu) attempting timestamp acquisition...",
         static_cast<unsigned long>(kMasterIntervalSeconds));
    (void)trackedHublinkSync(kMasterIntervalSeconds);
    if (rtcOkForLogging(probe))
    {
      ledGreenOff();
      dbgln(F("[rtc] valid; resuming wake."));
      return;
    }
    ledGreenOff();
    dbgf("[rtc] still invalid; rest %lums then retry sync.",
         static_cast<unsigned long>(kRtcRecoveryRestMs));
    delay(kRtcRecoveryRestMs);
  }
}

/// Enter deep sleep so total wake-to-wake time approximates kWakeCyclePeriodSeconds *excluding*
/// time spent inside hublink.sync() (a long gateway connection or RTC-recovery loop should not
/// compress the next sleep window). Does not return. Final sleep is clamped to
/// [kMinDeepSleepUs, kWakeCyclePeriodUs] so we never pass an invalid or runaway value to
/// esp_sleep_enable_timer_wakeup. Powers down the SD and aux-I2C rails first via the shared
/// HublinkNode API so neither gate draws current during sleep (PIN_SD_EN HIGH, PIN_I2C_EN HIGH).
static void enterDeepSleep()
{
  const uint32_t totalElapsedMs = millis();
  const uint32_t hublinkMs = gHublinkSyncTotalMs;
  // Defensive: hublinkMs should never exceed totalElapsedMs (we only add measured intervals to
  // it after each sync), but underflow on a uint32_t would produce a huge value, so guard.
  const uint32_t effectiveElapsedMs =
      (totalElapsedMs > hublinkMs) ? (totalElapsedMs - hublinkMs) : 0;
  const uint64_t effectiveElapsedUs = static_cast<uint64_t>(effectiveElapsedMs) * 1000ULL;

  uint64_t sleepUs;
  if (effectiveElapsedUs >= kWakeCyclePeriodUs)
  {
    // Effective wake work somehow exceeded the cycle (logic bug, or scan/SD overran). Don't
    // compress to floor and don't underflow — sleep one full cycle so the device returns to
    // nominal cadence on the wake after this one.
    sleepUs = kWakeCyclePeriodUs;
  }
  else
  {
    sleepUs = kWakeCyclePeriodUs - effectiveElapsedUs;
  }
  if (sleepUs < kMinDeepSleepUs)
  {
    sleepUs = kMinDeepSleepUs;
  }
  if (sleepUs > kWakeCyclePeriodUs)
  {
    // Belt-and-suspenders cap; should be unreachable given the branches above.
    sleepUs = kWakeCyclePeriodUs;
  }

  ledGreenOff();
  dbgln(F("[sleep] disabling external rails (SD + aux I2C)..."));
  Serial.flush();
  node.setExternalRailsEnabled(false);
  esp_sleep_enable_timer_wakeup(sleepUs);
  dbgf("[sleep] cycle=%lus total=%lums hublink=%lums effective=%lums sleep=%lums",
       static_cast<unsigned long>(kWakeCyclePeriodSeconds),
       static_cast<unsigned long>(totalElapsedMs),
       static_cast<unsigned long>(hublinkMs),
       static_cast<unsigned long>(effectiveElapsedMs),
       static_cast<unsigned long>(sleepUs / 1000ULL));
  Serial.flush();
  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  // beginHardware brings up PIN_USB_SENSE (input pullup) so readUsbSense is meaningful below.
  node.beginHardware();
  if (node.readUsbSense())
  {
    delay(kUsbSerialSettleMs);
  }
  dbgln(F("HublinkBLENode: Serial ready"));

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
    dbgln(F("[setup] SD mount failed; entering recovery wait."));
    waitForSdReady();
    ledGreenOn();
  }
  dbgf("[setup] SD ok, cardType=%u", static_cast<unsigned>(node.sd().cardType()));

  gAdvName = buildAdvName();
  Serial.print(F("[setup] advName="));
  Serial.println(gAdvName);
  Serial.flush();

  dbgln(F("[setup] hublink.begin()..."));
  if (!hublink.begin(gAdvName))
  {
    dbgln(F("[setup] Hublink begin failed; sleeping for retry."));
    enterDeepSleep();
  }
  dbgln(F("[setup] hublink.begin() returned true."));
  hublink.setTimestampCallback(onTimestampReceived);
  hublink.watchdogTimeoutMs = kHublinkWatchdogMs;

  // Block here if the DS3231 has lost time; we can't write meaningful CSV rows without a valid
  // unix timestamp. Returns once the gateway pushes a timestamp via setTimestampCallback.
  waitForRtcReady();

  // 3 quick blinks then LED OFF — visual cue for entering the sync window without holding the
  // LED on during the long sync.
  ledGreenBlinkNTimes(3, kLedQuickBlinkMs);
  dbgf("[wake] hublink.sync(%lu)...", static_cast<unsigned long>(kMasterIntervalSeconds));
  const bool syncOk = trackedHublinkSync(kMasterIntervalSeconds);
  dbgf("[wake] sync returned %s (cumulative sync ms=%lu)", syncOk ? "true" : "false",
       static_cast<unsigned long>(gHublinkSyncTotalMs));

  // 1 quick blink then LED OFF — visual cue for entering the scan window.
  ledGreenBlinkNTimes(1, kLedQuickBlinkMs);
  runBleScanWindowAndLogJbv();

  raven::RtcReading rtc;
  if (rtcOkForLogging(rtc))
  {
    dbgln(F("[wake] RTC ok — JXS/JXV may write."));
    ensureTodaysDailyCsvFiles(rtc);
    appendVitalsRow(rtc);
  }
  else
  {
    dbgln(F("[wake] RTC not valid — skip CSV writes; will retry next wake."));
  }

  // Free heap / min-ever heap / largest contiguous block / current task stack high-water mark.
  // ESP.getMinFreeHeap() and uxTaskGetStackHighWaterMark report the worst-case low water marks
  // since boot, which is more useful for spotting leaks/overflow than the instantaneous values.
  dbgf("[wake] mem heap_free=%lu min_free=%lu max_alloc=%lu stack_min_free=%lu",
       static_cast<unsigned long>(ESP.getFreeHeap()),
       static_cast<unsigned long>(ESP.getMinFreeHeap()),
       static_cast<unsigned long>(ESP.getMaxAllocHeap()),
       static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));

  enterDeepSleep();
}

void loop()
{
  // setup() never returns (it always calls esp_deep_sleep_start). loop() is intentionally empty.
}
