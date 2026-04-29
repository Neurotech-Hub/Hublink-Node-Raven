#include <HublinkNodeRaven.h>
#include <esp_sleep.h>

// HubWheelMinimal:
// - Core wheel logging + deep-sleep loop
// - No Hublink dependency
// - Uses fixed sketch defaults for cadence and file policy

raven::HublinkNode node;
raven::DataLoggerHelper logger(node);

// Core wheel timing policy.
constexpr uint32_t kSleepTimeSeconds = 10;
// File naming policy for logger helper.
constexpr char kLogBaseName[] = "HUBWHEEL";
constexpr raven::FileNameMode kLogFileMode = raven::FileNameMode::Daily;
constexpr bool kShowAwakeLed = true;
// Keep this sketch's default CSV focused on wheel and battery telemetry.
const raven::CsvFieldMask kLogFields = raven::csvFields({
    raven::CsvField::RtcUnix,
    raven::CsvField::MagnetPasses,
    raven::CsvField::BattV,
    raven::CsvField::BattPer,
});

struct LogContext {
  raven::LogFilePolicy filePolicy;
  raven::CsvFieldMask fieldMask = 0;
};

LogContext gLogContext = {
    {kLogBaseName, kLogFileMode, 0, false},
    kLogFields,
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

static void appendWheelLogRow()
{
  raven::SampleFields sample;
  // Shared helper handles path resolution, header-on-create, and row append.
  const raven::ServiceStatus logStatus = raven::captureAndAppendManagedCsv(
      logger, node, gLogContext.filePolicy, gLogContext.fieldMask, sample);
  if (logStatus != raven::ServiceStatus::Ok) {
    Serial.print(F("HubWheel: log write failed ("));
    Serial.print(raven::statusToString(logStatus));
    Serial.println(F(")"));
    return;
  }

  Serial.print(F("HubWheel: wheel count this sleep = "));
  Serial.print(sample.magnetPassCount);
  Serial.print(F(" (edges="));
  Serial.print(sample.ulpEdgeCount);
  Serial.println(F(")"));

  Serial.println(F("HubWheel: log write OK"));
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
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(kSleepTimeSeconds) * 1000000ULL);
  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  // Only wait for USB serial if USB is physically present.
  if (node.readUsbSense()) {
    const uint32_t serialWaitStartMs = millis();
    while (!Serial && (millis() - serialWaitStartMs) < 3000) {
      delay(10);
    }
  }
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

  Serial.println();
  Serial.println(F("--------- HubWheelMinimal Wake ---------"));
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

  Serial.println(F("--------- Entering deep sleep ----------"));
  Serial.println();

  enterSleep();
}

void loop() {}
