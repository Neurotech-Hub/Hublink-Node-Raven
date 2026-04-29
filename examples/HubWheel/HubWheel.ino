#include <HublinkNodeRaven.h>
#include <esp_sleep.h>

raven::HublinkNode node;
raven::DataLoggerHelper logger(node);

constexpr uint32_t kSleepTimeSeconds = 10;
constexpr uint32_t kSyncEveryMinutes = 360;
constexpr uint32_t kSyncForSeconds = 30;
constexpr char kLogBaseName[] = "HUBWHEEL";
constexpr raven::FileNameMode kLogFileMode = raven::FileNameMode::Daily;
constexpr bool kShowAwakeLed = true;
const raven::CsvFieldMask kLogFields = raven::csvFields({
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
    {kLogBaseName, kLogFileMode, 0, false},
    kLogFields,
};

static bool shouldSync(uint32_t sleepSeconds, uint32_t syncMinutes, uint32_t logCount)
{
  const float elapsedMinutes = (sleepSeconds * logCount) / 60.0f;
  return elapsedMinutes >= static_cast<float>(syncMinutes);
}

static void appendWheelLogRow()
{
  const raven::SampleFields sample = logger.captureSample();
  String logPath;
  if (raven::resolveLogFilePath(node.sd(), gLogContext.filePolicy, sample.rtc, logPath) !=
      raven::ServiceStatus::Ok) {
    Serial.println(F("HubWheel: invalid log filename policy"));
    return;
  }

  if (!node.sd().exists(logPath.c_str())) {
    if (node.sd().appendLine(logPath.c_str(), raven::DataLoggerHelper::csvHeader(gLogContext.fieldMask)) !=
        raven::ServiceStatus::Ok) {
      Serial.println(F("HubWheel: header write failed"));
      return;
    }
  }

  Serial.print(F("HubWheel: wheel count this sleep = "));
  Serial.print(sample.magnetPassCount);
  Serial.print(F(" (edges="));
  Serial.print(sample.ulpEdgeCount);
  Serial.println(F(")"));

  if (logger.appendCsvSample(logPath.c_str(), sample, gLogContext.fieldMask) ==
      raven::ServiceStatus::Ok)
  {
    gLogCount++;
    Serial.println(F("HubWheel: log write OK"));
  }
  else
  {
    Serial.println(F("HubWheel: log write failed"));
  }
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

  esp_sleep_wakeup_cause_t cause = node.wakeupCause();
  Serial.print(F("Wake cause: "));
  Serial.println(static_cast<int>(cause));

  // Match legacy behavior: only log when waking from timer sleep.
  if (node.isTimerWake())
  {
    appendWheelLogRow();
  }
  else
  {
    gLogCount = 0;
  }

  if (shouldSync(kSleepTimeSeconds, kSyncEveryMinutes, gLogCount))
  {
    Serial.print(F("HubWheel: sync window "));
    Serial.print(kSyncForSeconds);
    Serial.println(F("s (hook point for cloud sync)"));
    gLogCount = 0;
  }

  enterSleep();
}

void loop() {}
