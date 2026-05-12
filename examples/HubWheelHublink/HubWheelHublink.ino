#include <Hublink.h>
#include <HublinkNodeRaven.h>
#include <esp_sleep.h>

// HubWheelHublink:
// - Wheel logging + deep-sleep loop
// - Optional runtime tuning via meta.json through Hublink
// - Assumes Hublink library is installed
//
// Example meta.json used by this sketch:
//
// {
//   "hublink": {
//     "advertise": "HUBLINK",
//     "advertise_every": 120,
//     "advertise_for": 30,
//     "try_reconnect": false,
//     "reconnect_attempts": 2,
//     "reconnect_every": 30,
//     "upload_path": "/RAVEN",
//     "append_path": "device:id",
//     "disable": false
//   },
//   "wheel": {
//     "sleep_time_seconds": 10,
//     "sync_every_seconds": 21600,
//     "sync_for_seconds": 30
//   },
//   "logger": {
//     "log_base_name": "HUBWHEEL",
//     "log_file_mode": "daily",
//     "inc_on_reboot": false,
//     "log_fields": [
//       "datetime",
//       "magnet_passes",
//       "passes_min",
//       "batt_v",
//       "batt_per",
//      "temp_c",
//      "lux"
//     ]
//   },
//   "device": {
//     "id": "001"
//   }
// }
//
// Keys consumed directly by this sketch:
// - wheel.sleep_time_seconds, wheel.sync_every_seconds, wheel.sync_for_seconds
// - logger.log_base_name, logger.log_file_mode, logger.inc_on_reboot, logger.log_fields
// These wheel/logger keys are read from /meta.json by this library via raven::loadMetaJson
// (typed getters in MetaConfigReader), so sketches do not rely on Hublink for namespaces the
// device firmware owns here.
//
// hublink.* and device.* are still handled inside the Hublink library via hublink.begin().

raven::HublinkNode node;
raven::DataLoggerHelper logger(node);
raven::MetaConfigEditor metaEditor;
Hublink hublink(raven::PIN_SD_CS);

// Hardcoded defaults (meta.json can override these in beginHublink()).
uint32_t gSleepTimeSeconds = 10;
uint32_t gSyncEverySeconds = 21600;
uint32_t gSyncForSeconds = 30;
String gLogBaseName = "HUBWHEEL";
raven::FileNameMode gLogFileMode = raven::FileNameMode::Daily;
bool gIncOnReboot = false;
// Default CSV columns until meta.json `logger.log_fields` overrides (deep-sleep wheel + gauge).
static constexpr raven::CsvFieldMask kCsvFieldMask = raven::csvFields({
    raven::CsvField::RtcUnix,
    raven::CsvField::UlpEdges,
    raven::CsvField::MagnetPasses,
    raven::CsvField::PassesPerMin,
    raven::CsvField::BattV,
    raven::CsvField::BattPer,
});

RTC_DATA_ATTR uint32_t gLogCount = 0;

struct LogContext
{
  raven::LogFilePolicy filePolicy;
  raven::CsvFieldMask fieldMask = 0;
};

LogContext gLogContext = {
    {nullptr, raven::FileNameMode::Daily, 0, false},
    kCsvFieldMask,
};

static const __FlashStringHelper *wakeCauseText(esp_sleep_wakeup_cause_t cause)
{
  switch (cause)
  {
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return F("power_on_or_reset");
  case ESP_SLEEP_WAKEUP_EXT0:
    return F("ext0");
  case ESP_SLEEP_WAKEUP_EXT1:
    return F("ext1");
  case ESP_SLEEP_WAKEUP_TIMER:
    return F("timer");
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    return F("touchpad");
  case ESP_SLEEP_WAKEUP_ULP:
    return F("ulp");
  case ESP_SLEEP_WAKEUP_GPIO:
    return F("gpio");
  case ESP_SLEEP_WAKEUP_UART:
    return F("uart");
  default:
    return F("unknown");
  }
}

static void blinkPowerOnPattern(esp_sleep_wakeup_cause_t cause)
{
  if (cause != ESP_SLEEP_WAKEUP_UNDEFINED)
  {
    return;
  }
  pinMode(raven::PIN_LED_BLUE, OUTPUT);
  for (uint8_t i = 0; i < 3; ++i)
  {
    digitalWrite(raven::PIN_LED_GREEN, HIGH);
    digitalWrite(raven::PIN_LED_BLUE, HIGH);
    delay(100);
    digitalWrite(raven::PIN_LED_GREEN, LOW);
    digitalWrite(raven::PIN_LED_BLUE, LOW);
    delay(100);
  }
  digitalWrite(raven::PIN_LED_GREEN, HIGH);
}

static void blinkMissingSdCard()
{
  Serial.println(F("HubWheelHublink: SD card not present. Halting."));
  pinMode(raven::PIN_LED_BLUE, OUTPUT);
  while (true)
  {
    digitalWrite(raven::PIN_LED_GREEN, HIGH);
    digitalWrite(raven::PIN_LED_BLUE, LOW);
    delay(100);
    digitalWrite(raven::PIN_LED_GREEN, LOW);
    digitalWrite(raven::PIN_LED_BLUE, HIGH);
    delay(100);
  }
}

static void applyLogPolicyDefaults()
{
  gLogContext.filePolicy.baseName = gLogBaseName.c_str();
  gLogContext.filePolicy.mode = gLogFileMode;
  gLogContext.filePolicy.incOnReboot = gIncOnReboot;
}

static raven::FileNameMode parseLogFileMode(const String &modeText)
{
  String mode = modeText;
  mode.toLowerCase();
  mode.trim();
  if (mode == "daily")
  {
    return raven::FileNameMode::Daily;
  }
  if (mode == "hourly")
  {
    return raven::FileNameMode::Hourly;
  }
  if (mode == "manual")
  {
    return raven::FileNameMode::Manual;
  }
  if (mode == "disabled")
  {
    return raven::FileNameMode::Disabled;
  }
  return gLogFileMode;
}

static const __FlashStringHelper *logFileModeText(raven::FileNameMode mode)
{
  switch (mode)
  {
  case raven::FileNameMode::Daily:
    return F("daily");
  case raven::FileNameMode::Hourly:
    return F("hourly");
  case raven::FileNameMode::Manual:
    return F("manual");
  case raven::FileNameMode::Disabled:
    return F("disabled");
  }
  return F("unknown");
}

static void onTimestampReceived(uint32_t timestamp)
{
  // Lets gateway-provided timestamps update on-device RTC.
  node.rtc().adjust(DateTime(timestamp));
}

// Apply wheel/logger keys from meta.json via Raven helpers (SD + typed reads).
static void applyWheelLoggerMetaFromSd()
{
  JsonDocument metaDoc;
  if (!raven::loadMetaJson(node.sd(), metaDoc))
  {
    return;
  }

  uint32_t u = 0;
  if (raven::metaGetUInt32(metaDoc, String("wheel.sleep_time_seconds"), u))
  {
    gSleepTimeSeconds = u;
    Serial.print(F("wheel.sleep_time_seconds: "));
    Serial.println(gSleepTimeSeconds);
  }
  if (raven::metaGetUInt32(metaDoc, String("wheel.sync_every_seconds"), u))
  {
    gSyncEverySeconds = u;
    Serial.print(F("wheel.sync_every_seconds: "));
    Serial.println(gSyncEverySeconds);
  }
  if (raven::metaGetUInt32(metaDoc, String("wheel.sync_for_seconds"), u))
  {
    gSyncForSeconds = u;
    Serial.print(F("wheel.sync_for_seconds: "));
    Serial.println(gSyncForSeconds);
  }

  String s;
  if (raven::metaGetString(metaDoc, String("logger.log_base_name"), s))
  {
    gLogBaseName = s;
    Serial.print(F("logger.log_base_name: "));
    Serial.println(gLogBaseName);
  }

  String modeStr;
  if (raven::metaGetString(metaDoc, String("logger.log_file_mode"), modeStr))
  {
    gLogFileMode = parseLogFileMode(modeStr);
    Serial.print(F("logger.log_file_mode: "));
    Serial.println(logFileModeText(gLogFileMode));
  }

  bool rebootInc = false;
  if (raven::metaGetBool(metaDoc, String("logger.inc_on_reboot"), rebootInc))
  {
    gIncOnReboot = rebootInc;
    Serial.print(F("logger.inc_on_reboot: "));
    Serial.println(gIncOnReboot ? F("true") : F("false"));
  }

  JsonArrayConst fields{};
  if (raven::metaGetJsonArray(metaDoc, String("logger.log_fields"), fields))
  {
    const size_t fieldCount = fields.size();
    if (fieldCount > 0)
    {
      auto *fieldNames = new String[fieldCount];
      for (size_t fi = 0; fi < fieldCount; ++fi)
      {
        fieldNames[fi] = fields[fi].as<String>();
      }
      gLogContext.fieldMask = raven::buildCsvFieldMaskFromNames(
          fieldNames, fieldCount, gLogContext.fieldMask, &Serial);
      delete[] fieldNames;
      Serial.print(F("logger.log_fields applied: "));
      Serial.println(raven::DataLoggerHelper::csvHeader(gLogContext.fieldMask));
    }
  }
}

static void beginHublink()
{
  if (!hublink.begin())
  {
    Serial.println(F("HubWheelHublink: Hublink begin failed."));
    return;
  }

  Serial.println(F("HubWheelHublink: Hublink initialized."));
  hublink.setTimestampCallback(onTimestampReceived);

  applyWheelLoggerMetaFromSd();
  applyLogPolicyDefaults();
}

static void runHublinkSyncWindow()
{
  // Keep node characteristic battery level fresh before each sync attempt.
  // Dashboard policy for this sketch:
  // - Valid cell reading -> report actual SOC.
  // - No valid reading but USB present -> report 100% (externally powered lab setup).
  // - No valid reading and no USB -> report 0% to avoid stale/null data.
  const bool usbPresent = node.readUsbSense();
  const raven::BatteryReading battery = node.powerGauge().readSample();
  Serial.print(F("HubWheelHublink: battery status="));
  Serial.print(raven::statusToString(battery.status));
  Serial.print(F(" hasCell="));
  Serial.print(battery.hasCellReading ? F("true") : F("false"));
  Serial.print(F(" soc="));
  Serial.print(battery.stateOfChargePct, 1);
  Serial.print(F(" usbPresent="));
  Serial.println(usbPresent ? F("true") : F("false"));

  if (battery.status == raven::ServiceStatus::Ok && battery.hasCellReading &&
      battery.stateOfChargePct > 0.0f)
  {
    const int batteryPct = static_cast<int>(battery.stateOfChargePct + 0.5f);
    hublink.setBatteryLevel(static_cast<uint8_t>(constrain(batteryPct, 0, 100)));
    Serial.print(F("HubWheelHublink: setBatteryLevel from gauge="));
    Serial.println(batteryPct);
  }
  else if (usbPresent)
  {
    hublink.setBatteryLevel(100);
    Serial.println(F("HubWheelHublink: setBatteryLevel fallback=100 (USB present)"));
  }
  else
  {
    hublink.setBatteryLevel(0);
    Serial.println(F("HubWheelHublink: setBatteryLevel fallback=0 (no USB / no valid gauge)"));
  }
  // Uses existing Hublink config from meta.json/hardcoded defaults.
  hublink.sync(gSyncForSeconds);
}

static void appendWheelLogRow()
{
  raven::SampleFields sample;
  sample = logger.captureSample();
  sample.passesPerMin =
      raven::computePassesPerMinute(sample.magnetPassCount, gSleepTimeSeconds);

  String logPath;
  const raven::ServiceStatus pathStatus =
      raven::resolveLogFilePath(node.sd(), gLogContext.filePolicy, sample.rtc, logPath);
  if (pathStatus != raven::ServiceStatus::Ok)
  {
    Serial.print(F("HubWheelHublink: log write failed ("));
    Serial.print(raven::statusToString(pathStatus));
    Serial.println(F(")"));
    return;
  }

  if (!node.sd().exists(logPath.c_str()))
  {
    const String header = gLogContext.fieldMask == 0
                              ? raven::DataLoggerHelper::csvHeader()
                              : raven::DataLoggerHelper::csvHeader(gLogContext.fieldMask);
    const raven::ServiceStatus headerStatus = node.sd().appendLine(logPath.c_str(), header);
    if (headerStatus != raven::ServiceStatus::Ok)
    {
      Serial.print(F("HubWheelHublink: log write failed ("));
      Serial.print(raven::statusToString(headerStatus));
      Serial.println(F(")"));
      return;
    }
  }

  const raven::ServiceStatus rowStatus = gLogContext.fieldMask == 0
                                             ? logger.appendCsvSample(logPath.c_str(), sample)
                                             : logger.appendCsvSample(logPath.c_str(), sample,
                                                                      gLogContext.fieldMask);
  if (rowStatus != raven::ServiceStatus::Ok)
  {
    Serial.print(F("HubWheelHublink: log write failed ("));
    Serial.print(raven::statusToString(rowStatus));
    Serial.println(F(")"));
    return;
  }

  Serial.print(F("HubWheelHublink: wheel count this sleep = "));
  Serial.print(sample.magnetPassCount);
  Serial.print(F(" (edges="));
  Serial.print(sample.ulpEdgeCount);
  Serial.println(F(")"));

  gLogCount++;
  Serial.println(F("HubWheelHublink: log write OK"));
}

static void enterSleep()
{
  digitalWrite(raven::PIN_LED_GREEN, LOW);
  node.magnetCounter().clearCount();
  node.magnetCounter().begin();
  node.magnetCounter().start();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(gSleepTimeSeconds) * 1000000ULL);
  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  pinMode(raven::PIN_LED_GREEN, OUTPUT);
  digitalWrite(raven::PIN_LED_GREEN, LOW);
  // Keep LED on while awake; turn off immediately before deep sleep.
  digitalWrite(raven::PIN_LED_GREEN, HIGH);
  node.beginHardware();
  node.beginI2C();
  logger.begin();
  if (!node.sd().begin() || node.sd().cardType() == CARD_NONE)
  {
    blinkMissingSdCard();
  }
  const esp_sleep_wakeup_cause_t cause = node.wakeupCause();
  // Offer editor only on power-on/reset wake, not deep-sleep wake cycles.
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && node.readUsbSense())
  {
    metaEditor.maybeEnterWithFade(node.sd(), true, Serial, 3000,
                                  raven::PIN_LED_GREEN, raven::PIN_LED_BLUE, &node);
  }
  applyLogPolicyDefaults();
  beginHublink();

  Serial.println();
  Serial.println(F("--------- HubWheelHublink Wake ---------"));
  Serial.print(F("Wake cause: "));
  Serial.print(wakeCauseText(cause));
  Serial.print(F(" ("));
  Serial.print(static_cast<int>(cause));
  Serial.println(F(")"));
  Serial.println();
  blinkPowerOnPattern(cause);

  // Match legacy behavior: only log when waking from timer sleep.
  if (node.isTimerWake())
  {
    appendWheelLogRow();
  }
  else
  {
    gLogCount = 0;
  }

  if (raven::shouldRunSyncWindow(gSleepTimeSeconds, gSyncEverySeconds, gLogCount))
  {
    Serial.print(F("HubWheelHublink: sync window "));
    Serial.print(gSyncForSeconds);
    Serial.println(F("s"));
    runHublinkSyncWindow();
    gLogCount = 0;
  }
  else
  {
    Serial.println(F("HubWheelHublink: sync window skipped"));
  }

  Serial.println(F("--------- Entering deep sleep ----------"));
  Serial.println();

  enterSleep();
}

void loop() {}
