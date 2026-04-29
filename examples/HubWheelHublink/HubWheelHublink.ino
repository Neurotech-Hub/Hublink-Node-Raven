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
//     "log_fields": [
//       "rtc_unix",
//       "rtc_text",
//       "magnet_passes",
//       "batt_v",
//       "batt_per"
//     ]
//   },
//   "device": {
//     "id": "001"
//   }
// }
//
// Keys consumed directly by this sketch:
// - wheel.sleep_time_seconds, wheel.sync_every_seconds, wheel.sync_for_seconds
// - logger.log_base_name, logger.log_file_mode, logger.log_fields
//
// hublink.* and device.* are handled by the Hublink library itself.

raven::HublinkNode node;
raven::DataLoggerHelper logger(node);
Hublink hublink(raven::PIN_SD_CS);

// Hardcoded defaults (meta.json can override these in beginHublink()).
uint32_t gSleepTimeSeconds = 10;
uint32_t gSyncEverySeconds = 21600;
uint32_t gSyncForSeconds = 30;
String gLogBaseName = "HUBWHEEL";
raven::FileNameMode gLogFileMode = raven::FileNameMode::Daily;
constexpr bool kShowAwakeLed = true;
const raven::CsvFieldMask kDefaultLogFields = raven::csvFields({
    raven::CsvField::RtcUnix,
    raven::CsvField::MagnetPasses,
    raven::CsvField::BattV,
    raven::CsvField::BattPer,
});

RTC_DATA_ATTR uint32_t gLogCount = 0;

struct LogContext {
  raven::LogFilePolicy filePolicy;
  raven::CsvFieldMask fieldMask = 0;
};

LogContext gLogContext = {
    {nullptr, raven::FileNameMode::Daily, 0, false},
    kDefaultLogFields,
};

static const __FlashStringHelper *wakeCauseText(esp_sleep_wakeup_cause_t cause)
{
  switch (cause)
  {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return F("power_on_or_reset");
    case ESP_SLEEP_WAKEUP_EXT0: return F("ext0");
    case ESP_SLEEP_WAKEUP_EXT1: return F("ext1");
    case ESP_SLEEP_WAKEUP_TIMER: return F("timer");
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return F("touchpad");
    case ESP_SLEEP_WAKEUP_ULP: return F("ulp");
    case ESP_SLEEP_WAKEUP_GPIO: return F("gpio");
    case ESP_SLEEP_WAKEUP_UART: return F("uart");
    default: return F("unknown");
  }
}

static void applyLogPolicyDefaults() {
  gLogContext.filePolicy.baseName = gLogBaseName.c_str();
  gLogContext.filePolicy.mode = gLogFileMode;
}

static raven::FileNameMode parseLogFileMode(const String &modeText) {
  String mode = modeText;
  mode.toLowerCase();
  mode.trim();
  if (mode == "daily") {
    return raven::FileNameMode::Daily;
  }
  if (mode == "hourly") {
    return raven::FileNameMode::Hourly;
  }
  if (mode == "manual") {
    return raven::FileNameMode::Manual;
  }
  if (mode == "disabled") {
    return raven::FileNameMode::Disabled;
  }
  return gLogFileMode;
}

static const __FlashStringHelper *logFileModeText(raven::FileNameMode mode) {
  switch (mode) {
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

static void onTimestampReceived(uint32_t timestamp) {
  // Lets gateway-provided timestamps update on-device RTC.
  node.rtc().adjust(DateTime(timestamp));
}

static void beginHublink() {
  if (!hublink.begin()) {
    Serial.println(F("HubWheelHublink: Hublink begin failed."));
    return;
  }

  Serial.println(F("HubWheelHublink: Hublink initialized."));
  hublink.setTimestampCallback(onTimestampReceived);

  // wheel namespace: wheel-specific behavior/timing values.
  if (hublink.hasMetaKey("wheel", "sleep_time_seconds")) {
    gSleepTimeSeconds = hublink.getMeta<int>("wheel", "sleep_time_seconds");
    Serial.print(F("wheel.sleep_time_seconds: "));
    Serial.println(gSleepTimeSeconds);
  }
  if (hublink.hasMetaKey("wheel", "sync_every_seconds")) {
    gSyncEverySeconds = hublink.getMeta<int>("wheel", "sync_every_seconds");
    Serial.print(F("wheel.sync_every_seconds: "));
    Serial.println(gSyncEverySeconds);
  }
  if (hublink.hasMetaKey("wheel", "sync_for_seconds")) {
    gSyncForSeconds = hublink.getMeta<int>("wheel", "sync_for_seconds");
    Serial.print(F("wheel.sync_for_seconds: "));
    Serial.println(gSyncForSeconds);
  }

  // logger namespace: cross-sketch file naming + field selection values.
  if (hublink.hasMetaKey("logger", "log_base_name")) {
    gLogBaseName = hublink.getMeta<String>("logger", "log_base_name");
    Serial.print(F("logger.log_base_name: "));
    Serial.println(gLogBaseName);
  }
  if (hublink.hasMetaKey("logger", "log_file_mode")) {
    gLogFileMode = parseLogFileMode(hublink.getMeta<String>("logger", "log_file_mode"));
    Serial.print(F("logger.log_file_mode: "));
    Serial.println(logFileModeText(gLogFileMode));
  }
  if (hublink.hasMetaKey("logger", "log_fields")) {
    const JsonArray fields = hublink.getMeta<JsonArray>("logger", "log_fields");
    const size_t fieldCount = fields.size();
    if (fieldCount > 0) {
      String *fieldNames = new String[fieldCount];
      size_t i = 0;
      for (JsonVariant fieldValue : fields) {
        fieldNames[i++] = fieldValue.as<String>();
      }
      gLogContext.fieldMask = raven::buildCsvFieldMaskFromNames(
          fieldNames, fieldCount, gLogContext.fieldMask, &Serial);
      delete[] fieldNames;
      Serial.print(F("logger.log_fields applied: "));
      Serial.println(raven::DataLoggerHelper::csvHeader(gLogContext.fieldMask));
    }
  }

  applyLogPolicyDefaults();
}

static void runHublinkSyncWindow() {
  // Keep node characteristic battery level fresh before each sync attempt.
  // Dashboard policy for this sketch:
  // - Valid cell reading -> report actual SOC.
  // - No valid reading but USB present -> report 100% (externally powered lab setup).
  // - No valid reading and no USB -> leave battery level unchanged.
  const raven::BatteryReading battery = node.powerGauge().readSample();
  if (battery.status == raven::ServiceStatus::Ok && battery.hasCellReading) {
    const int batteryPct = static_cast<int>(battery.stateOfChargePct + 0.5f);
    hublink.setBatteryLevel(static_cast<uint8_t>(constrain(batteryPct, 0, 100)));
  } else if (node.readUsbSense()) {
    hublink.setBatteryLevel(100);
  }
  // Uses existing Hublink config from meta.json/hardcoded defaults.
  hublink.sync(gSyncForSeconds);
}

static void appendWheelLogRow()
{
  raven::SampleFields sample;
  // Shared helper handles path resolution, header-on-create, and row append.
  const raven::ServiceStatus logStatus = raven::captureAndAppendManagedCsv(
      logger, node, gLogContext.filePolicy, gLogContext.fieldMask, sample);
  if (logStatus != raven::ServiceStatus::Ok) {
    Serial.print(F("HubWheelHublink: log write failed ("));
    Serial.print(raven::statusToString(logStatus));
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
  if (kShowAwakeLed)
  {
    digitalWrite(raven::PIN_LED_GREEN, LOW);
  }
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
  if (kShowAwakeLed)
  {
    // Keep LED on while awake; turn off immediately before deep sleep.
    digitalWrite(raven::PIN_LED_GREEN, HIGH);
  }
  node.beginHardware();
  node.beginI2C();
  logger.begin();
  applyLogPolicyDefaults();
  beginHublink();

  Serial.println();
  Serial.println(F("--------- HubWheelHublink Wake ---------"));
  esp_sleep_wakeup_cause_t cause = node.wakeupCause();
  Serial.print(F("Wake cause: "));
  Serial.print(wakeCauseText(cause));
  Serial.print(F(" ("));
  Serial.print(static_cast<int>(cause));
  Serial.println(F(")"));
  Serial.println();

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
