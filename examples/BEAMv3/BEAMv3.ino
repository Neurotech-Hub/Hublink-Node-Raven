// BEAMv3 — Raven hardware BEAM-style motion logger (ULP on PIN_AUX_GPIO1 + SD CSV).
//
// meta.json schema matches legacy Hublink-BEAM README (beam.*, device.*, hublink.*).
// Hublink BLE sync is stubbed out this sprint — see commented block in setup().
//
// Power-on: pre-editor init (like MetaConfigEditorHold) → maybeEnterWithFade (USB) →
// boot wait fade → load meta once → logging init → log → deep sleep.
// Timer wake: pre-editor init (incl. min battery gate) → load meta → logging → deep sleep.

#include <ArduinoJson.h>
#include <HublinkNodeRaven.h>
#include <algorithm>
#include <esp_sleep.h>

// --- meta.json defaults (overridden when keys present) ---
static constexpr uint32_t kDefaultLogEveryMinutes = 1;
static constexpr bool kDefaultNewFileOnBoot = false;
static constexpr uint32_t kDefaultInactivityPeriodSeconds = 40;
static constexpr char kDefaultDeviceId[] = "001";
static constexpr char kLibraryVersion[] = "0.2.1";

static constexpr uint32_t kUsbSerialSettleMs = 2000;
static constexpr uint32_t kErrorBlinkMs = 200;
static constexpr uint32_t kBootDebounceMs = 80;
static constexpr float kMinBatteryVolts = 3.7f;

raven::HublinkNode node;
raven::MetaConfigEditor metaEditor;

RTC_DATA_ATTR uint32_t gSleepStartUnix = 0;

static uint32_t gLogEveryMinutes = kDefaultLogEveryMinutes;
static bool gNewFileOnBoot = kDefaultNewFileOnBoot;
static uint32_t gInactivityPeriodSeconds = kDefaultInactivityPeriodSeconds;
static String gDeviceId = kDefaultDeviceId;
// Read from meta but unused while Hublink sync is stubbed:
static uint32_t gSyncEveryMinutes = 3;
static uint32_t gSyncForSeconds = 30;
static uint32_t gRandomizeAlarmMinutes = 0;

static String gActiveLogPath;

static void applyMetaFromDoc(const JsonDocument &doc)
{
  long v = 0;
  if (raven::metaGetLong(doc, String("beam.log_every_minutes"), v) && v > 0)
  {
    gLogEveryMinutes = static_cast<uint32_t>(v);
  }
  bool b = false;
  if (raven::metaGetBool(doc, String("beam.new_file_on_boot"), b))
  {
    gNewFileOnBoot = b;
  }
  if (raven::metaGetLong(doc, String("beam.inactivity_period_seconds"), v) && v > 0)
  {
    gInactivityPeriodSeconds = static_cast<uint32_t>(v);
  }
  if (raven::metaGetLong(doc, String("beam.sync_every_minutes"), v) && v > 0)
  {
    gSyncEveryMinutes = static_cast<uint32_t>(v);
  }
  if (raven::metaGetLong(doc, String("beam.sync_for_seconds"), v) && v > 0)
  {
    gSyncForSeconds = static_cast<uint32_t>(v);
  }
  if (raven::metaGetLong(doc, String("beam.randomize_alarm_minutes"), v) && v >= 0)
  {
    gRandomizeAlarmMinutes = static_cast<uint32_t>(v);
  }
  String id;
  if (raven::metaGetString(doc, String("device.id"), id) && id.length() > 0)
  {
    gDeviceId = id;
  }
}

static bool reloadMetaFromSd()
{
  StaticJsonDocument<2048> metaDoc;
  if (!raven::loadMetaJson(node.sd(), metaDoc, "/meta.json", &Serial))
  {
    Serial.println(F("BEAMv3: /meta.json missing or invalid"));
    return false;
  }
  applyMetaFromDoc(metaDoc);
  return true;
}

static void errorBlinkForever()
{
  pinMode(raven::PIN_LED_GREEN, OUTPUT);
  pinMode(raven::PIN_LED_BLUE, OUTPUT);
  bool greenOn = true;
  while (true)
  {
    digitalWrite(raven::PIN_LED_GREEN, greenOn ? HIGH : LOW);
    digitalWrite(raven::PIN_LED_BLUE, greenOn ? LOW : HIGH);
    greenOn = !greenOn;
    delay(kErrorBlinkMs);
  }
}

static bool checkMinBatteryVoltage()
{
  if (node.readUsbSense())
  {
    return true;
  }

  const raven::BatteryReading batt = node.powerGauge().readSample();
  if (batt.status != raven::ServiceStatus::Ok || !batt.hasCellReading)
  {
    Serial.println(F("BEAMv3: battery voltage unavailable (skip min-voltage gate)"));
    return true;
  }
  if (batt.voltageV < kMinBatteryVolts)
  {
    Serial.print(F("BEAMv3: battery below minimum: "));
    Serial.print(batt.voltageV, 3);
    Serial.print(F(" V (need >= "));
    Serial.print(kMinBatteryVolts, 1);
    Serial.println(F(" V)"));
    return false;
  }
  return true;
}

static bool initBeamPreEditor()
{
  node.beginHardware();
  node.setI2CPowerEnabled(true);
  if (!node.beginI2C())
  {
    Serial.println(F("BEAMv3: beginI2C failed"));
    return false;
  }
  node.rtc().begin();
  node.powerGauge().begin();
  if (!checkMinBatteryVoltage())
  {
    return false;
  }
  node.light().begin();
  node.environment().begin();
  if (!node.sd().begin() || node.sd().cardType() == CARD_NONE)
  {
    Serial.println(F("BEAMv3: SD mount failed"));
    return false;
  }
  return true;
}

static bool finalizeBeamCoreAfterMeta()
{
  if (!reloadMetaFromSd())
  {
    return false;
  }

  const raven::RtcReading rtc = node.rtc().readSample();
  if (rtc.status != raven::ServiceStatus::Ok || !rtc.now.isValid())
  {
    Serial.println(F("BEAMv3: RTC not valid"));
    return false;
  }

  Serial.println(F("BEAMv3: init OK"));
  Serial.printf("  device_id=%s log_every_minutes=%lu inactivity_period_s=%lu new_file_on_boot=%s\n",
                gDeviceId.c_str(), static_cast<unsigned long>(gLogEveryMinutes),
                static_cast<unsigned long>(gInactivityPeriodSeconds),
                gNewFileOnBoot ? "true" : "false");
  return true;
}

static bool initBeamLogging()
{
  // Sensors and SD were already initialized in initBeamPreEditor (MetaConfigEditorHold path).
  Serial.println(F("BEAMv3: logging init"));
  Serial.flush();
  raven::maybeAutomaticVoltageSafeguard(node, true);
  return true;
}

static void stepSyncedFade(int &duty, int &step)
{
  analogWrite(raven::PIN_LED_GREEN, duty);
  analogWrite(raven::PIN_LED_BLUE, duty);
  duty += step;
  if (duty >= 255)
  {
    duty = 255;
    step = -step;
  }
  else if (duty <= 0)
  {
    duty = 0;
    step = -step;
  }
}

static void waitBootReleaseWithFade()
{
  Serial.println(F("Press and release BOOT to start logging."));

  pinMode(raven::PIN_LED_GREEN, OUTPUT);
  pinMode(raven::PIN_LED_BLUE, OUTPUT);
  pinMode(raven::PIN_BOOT_BUTTON, INPUT_PULLUP);

  int duty = 0;
  int step = 12;
  bool seenPress = false;

  while (true)
  {
    const bool bootLow = digitalRead(raven::PIN_BOOT_BUTTON) == LOW;
    if (bootLow)
    {
      seenPress = true;
    }
    else if (seenPress)
    {
      delay(kBootDebounceMs);
      if (digitalRead(raven::PIN_BOOT_BUTTON) != LOW)
      {
        analogWrite(raven::PIN_LED_GREEN, 0);
        analogWrite(raven::PIN_LED_BLUE, 0);
        return;
      }
    }

    stepSyncedFade(duty, step);
    delay(25);
  }
}

static bool resolveActiveLogPath(bool isWakeFromSleep, String &outPath)
{
  const raven::RtcReading rtc = node.rtc().readSample();
  if (rtc.status != raven::ServiceStatus::Ok || !rtc.now.isValid())
  {
    Serial.println(F("BEAMv3: RTC invalid — cannot resolve log path"));
    return false;
  }

  raven::BeamFileNamingParams naming;
  naming.deviceId = gDeviceId.c_str();
  naming.newFileOnBoot = gNewFileOnBoot;
  naming.isWakeFromSleep = isWakeFromSleep;

  outPath = raven::resolveBeamLogFilePath(node.sd(), naming, rtc.now);
  if (outPath.length() == 0)
  {
    Serial.println(F("BEAMv3: no available log filename"));
    return false;
  }
  return true;
}

static bool ensureBeamLogFile(bool isWakeFromSleep)
{
  if (!resolveActiveLogPath(isWakeFromSleep, gActiveLogPath))
  {
    return false;
  }

  if (!raven::BeamCsvLogger::ensureLogFile(node.sd(), gActiveLogPath.c_str()))
  {
    Serial.println(F("BEAMv3: failed to create log file"));
    return false;
  }

  Serial.printf("BEAMv3: log file ready %s\n", gActiveLogPath.c_str());
  return true;
}

static void reportLogResult(bool ok, const String &row)
{
  if (!node.readUsbSense())
  {
    return;
  }

  Serial.println(ok ? F("BEAMv3: log write OK") : F("BEAMv3: log write FAILED"));
  Serial.print(F("  path: "));
  Serial.println(gActiveLogPath);
  if (ok && row.length() > 0)
  {
    Serial.print(F("  row: "));
    Serial.println(row);
  }
  Serial.flush();
}

static void fillBeamLogSample(raven::BeamLogSample &out, bool isWakeFromSleep, bool isRebootRow)
{
  const uint16_t activityCount = node.motionCounter().motionCount();
  const uint16_t inactivityCount = node.motionCounter().inactivityCount();

  out.millisStamp = millis();
  out.deviceId = gDeviceId.c_str();
  out.libraryVersion = kLibraryVersion;
  out.activityCount = activityCount;
  out.inactivityPeriodS = static_cast<uint16_t>(gInactivityPeriodSeconds);
  out.inactivityCount = inactivityCount;
  out.minFreeHeap = ESP.getMinFreeHeap();
  out.reboot = isRebootRow ? 1 : 0;
  out.activityPercent = 0.0;
  out.inactivityPercent = 0.0;

  const raven::RtcReading rtc = node.rtc().readSample();
  if (rtc.status == raven::ServiceStatus::Ok && rtc.now.isValid())
  {
    out.year = rtc.now.year();
    out.month = rtc.now.month();
    out.day = rtc.now.day();
    out.hour = rtc.now.hour();
    out.minute = rtc.now.minute();
    out.second = rtc.now.second();

    if (isWakeFromSleep && gSleepStartUnix > 0)
    {
      const uint32_t nowUnix = rtc.now.unixtime();
      const double elapsedSeconds =
          (nowUnix > gSleepStartUnix) ? static_cast<double>(nowUnix - gSleepStartUnix) : 0.0;
      const double activeSeconds = static_cast<double>(activityCount);
      if (elapsedSeconds > 0.0)
      {
        out.activityPercent = std::min(1.0, activeSeconds / elapsedSeconds);
      }
      if (gInactivityPeriodSeconds > 0)
      {
        const double inactiveSeconds =
            static_cast<double>(inactivityCount) *
            static_cast<double>(gInactivityPeriodSeconds);
        const double denom = inactiveSeconds + activeSeconds;
        if (denom > 0.0)
        {
          out.inactivityPercent = std::min(1.0, inactiveSeconds / denom);
        }
      }
    }
  }

  const raven::BatteryReading batt = node.powerGauge().readSample();
  if (batt.status == raven::ServiceStatus::Ok)
  {
    out.batteryVoltage = batt.voltageV;
  }

  const raven::EnvReading env = node.environment().readSample();
  if (env.status == raven::ServiceStatus::Ok)
  {
    out.temperatureC = env.temperatureC;
    out.pressureHpa = env.pressureHpa;
    out.humidityPct = env.humidityPct;
  }

  const raven::LightReading light = node.light().readSample();
  if (light.status == raven::ServiceStatus::Ok)
  {
    out.lux = light.lux;
  }
}

static bool appendBeamLogRow(bool isWakeFromSleep, bool isRebootRow, String &rowOut)
{
  rowOut = "";
  raven::BeamLogSample sample;
  fillBeamLogSample(sample, isWakeFromSleep, isRebootRow);

  const raven::RtcReading rtc = node.rtc().readSample();
  if (rtc.status != raven::ServiceStatus::Ok || !rtc.now.isValid())
  {
    Serial.println(F("BEAMv3: skip log — RTC invalid"));
    return false;
  }

  String logPath = gActiveLogPath;
  if (logPath.length() == 0)
  {
    if (!resolveActiveLogPath(isWakeFromSleep, logPath))
    {
      return false;
    }
    gActiveLogPath = logPath;
  }

  if (!raven::BeamCsvLogger::ensureLogFile(node.sd(), logPath.c_str()))
  {
    Serial.println(F("BEAMv3: failed to ensure log file before append"));
    return false;
  }

  rowOut = raven::BeamCsvLogger::formatRow(sample);

  if (node.sd().appendLine(logPath.c_str(), rowOut) != raven::ServiceStatus::Ok)
  {
    Serial.println(F("BEAMv3: failed to append CSV row"));
    return false;
  }

  if (sample.activityCount > 0)
  {
    digitalWrite(raven::PIN_LED_GREEN, HIGH);
    delay(100);
    digitalWrite(raven::PIN_LED_GREEN, LOW);
  }

  Serial.printf("BEAMv3: logged activity_count=%u path=%s\n",
                static_cast<unsigned>(sample.activityCount), logPath.c_str());
  return true;
}

static void enterDeepSleep()
{
  node.motionCounter().setInactivityPeriod(static_cast<uint16_t>(gInactivityPeriodSeconds));
  node.motionCounter().begin(static_cast<gpio_num_t>(raven::PIN_AUX_GPIO1));

  digitalWrite(raven::PIN_LED_GREEN, LOW);
  digitalWrite(raven::PIN_LED_BLUE, LOW);
  node.sd().end();
  node.setExternalRailsEnabled(false);

  const uint64_t sleepUs = static_cast<uint64_t>(gLogEveryMinutes) * 60ULL * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepUs);
  Serial.printf("BEAMv3: deep sleep %lu min (%llu us)\n",
                static_cast<unsigned long>(gLogEveryMinutes),
                static_cast<unsigned long long>(sleepUs));
  Serial.flush();

  const raven::RtcReading rtc = node.rtc().readSample();
  if (rtc.status == raven::ServiceStatus::Ok && rtc.now.isValid())
  {
    gSleepStartUnix = rtc.now.unixtime();
  }

  node.motionCounter().clearCount();
  if (!node.motionCounter().start())
  {
    Serial.println(F("BEAMv3: ULP start failed; halting."));
    errorBlinkForever();
  }

  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  if (node.readUsbSense())
  {
    delay(kUsbSerialSettleMs);
  }

  const esp_sleep_wakeup_cause_t wakeCause = node.wakeupCause();
  const bool isPowerOnReset = (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED);
  const bool isTimerWake = node.isTimerWake();

  if (!initBeamPreEditor())
  {
    errorBlinkForever();
  }

  if (isPowerOnReset)
  {
    if (node.readUsbSense())
    {
      metaEditor.maybeEnterWithFade(node.sd(), true, Serial, 3000,
                                    raven::PIN_LED_GREEN, raven::PIN_LED_BLUE, &node);
    }
    waitBootReleaseWithFade();
    Serial.println(F("BEAMv3: boot gate complete"));
  }

  if (!finalizeBeamCoreAfterMeta())
  {
    errorBlinkForever();
  }

  if (!initBeamLogging())
  {
    errorBlinkForever();
  }

  /*
   * Future Hublink sync (requires Hublink library + NimBLE):
   *
   * Hublink hublink(raven::PIN_SD_CS);
   * if (hublink.begin(gDeviceId)) {
   *   hublink.setTimestampCallback(...);
   *   if (!meta hublink.disable) {
   *     if (isPowerOn || alarmElapsed(gSyncEveryMinutes)) {
   *       hublink.sync(gSyncForSeconds);
   *     }
   *   }
   * }
   */

  (void)gSyncEveryMinutes;
  (void)gSyncForSeconds;
  (void)gRandomizeAlarmMinutes;

  Serial.println(F("BEAMv3: preparing log file"));
  Serial.flush();

  if (!ensureBeamLogFile(isTimerWake))
  {
    if (node.readUsbSense())
    {
      reportLogResult(false, String());
    }
    errorBlinkForever();
  }

  String loggedRow;
  Serial.println(F("BEAMv3: appending log row"));
  Serial.flush();
  const bool logged = appendBeamLogRow(isTimerWake, isPowerOnReset, loggedRow);
  reportLogResult(logged, loggedRow);
  Serial.println(F("BEAMv3: entering deep sleep"));
  Serial.flush();
  enterDeepSleep();
}

void loop()
{
  // setup() never returns (esp_deep_sleep_start). loop() is intentionally empty.
}
