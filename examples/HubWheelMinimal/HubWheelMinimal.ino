#include <HublinkNodeRaven.h>
#include <esp_sleep.h>

// HubWheelMinimal:
// - Core wheel logging + deep-sleep loop (same flow as HubWheelHublink without the Hublink library).
// - Optional USB Serial MetaConfigEditor on power-on (same hold as HubWheelHublink); no gateway sync.
// - Hardcoded sleep interval, log file policy, and default CSV columns match HubWheelHublink when
//   meta.json is absent; see examples/HubWheelHublink/HubWheelHublink.ino for wheel.* / logger.*
//   keys and the sample meta.json comment block.

raven::HublinkNode node;
raven::DataLoggerHelper logger(node);
raven::MetaConfigEditor metaEditor;

// Core wheel timing policy.
constexpr uint32_t kSleepTimeSeconds = 10;
// File naming policy for logger helper.
constexpr char kLogBaseName[] = "HUBWHEEL";
constexpr raven::FileNameMode kLogFileMode = raven::FileNameMode::Daily;
// Default CSV columns: deep-sleep wheel + gauge (same mask order as HubWheelHublink kCsvFieldMask).
static constexpr raven::CsvFieldMask kCsvFieldMask = raven::csvFields({
    raven::CsvField::RtcUnix,
    raven::CsvField::UlpEdges,
    raven::CsvField::MagnetPasses,
    raven::CsvField::PassesPerMin,
    raven::CsvField::BattV,
    raven::CsvField::BattPer,
});

struct LogContext
{
  raven::LogFilePolicy filePolicy;
  raven::CsvFieldMask fieldMask = 0;
};

LogContext gLogContext = {
    {kLogBaseName, kLogFileMode, 0, false},
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
  Serial.println(F("HubWheel: SD card not present. Halting."));
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

static void appendWheelLogRow()
{
  raven::SampleFields sample;
  sample = logger.captureSample();
  sample.passesPerMin =
      raven::computePassesPerMinute(sample.magnetPassCount, kSleepTimeSeconds);

  String logPath;
  const raven::ServiceStatus pathStatus =
      raven::resolveLogFilePath(node.sd(), gLogContext.filePolicy, sample.rtc, logPath);
  if (pathStatus != raven::ServiceStatus::Ok)
  {
    Serial.print(F("HubWheel: log write failed ("));
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
      Serial.print(F("HubWheel: log write failed ("));
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
    Serial.print(F("HubWheel: log write failed ("));
    Serial.print(raven::statusToString(rowStatus));
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
  digitalWrite(raven::PIN_LED_GREEN, LOW);
  node.magnetCounter().clearCount();
  node.magnetCounter().begin();
  node.magnetCounter().start();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(kSleepTimeSeconds) * 1000000ULL);
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
  // Offer editor only on power-on/reset wake, not deep-sleep wake cycles (same as HubWheelHublink).
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && node.readUsbSense())
  {
    metaEditor.maybeEnterWithFade(node.sd(), true, Serial, 3000,
                                  raven::PIN_LED_GREEN, raven::PIN_LED_BLUE, &node);
  }

  Serial.println();
  Serial.println(F("--------- HubWheelMinimal Wake ---------"));
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

  Serial.println(F("--------- Entering deep sleep ----------"));
  Serial.println();

  enterSleep();
}

void loop() {}
