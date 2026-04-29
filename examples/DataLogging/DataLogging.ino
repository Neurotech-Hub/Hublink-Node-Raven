#include <HublinkNodeRaven.h>

raven::HublinkNode node;
raven::DataLoggerHelper logger(node);

constexpr char kLogBaseName[] = "LOGGER";
constexpr raven::FileNameMode kLogFileMode = raven::FileNameMode::Disabled;

raven::LogFilePolicy gLogFilePolicy = {
    kLogBaseName,
    kLogFileMode,
    0,
    false,
};

void setup()
{
  Serial.begin(115200);
  pinMode(raven::PIN_LED_GREEN, OUTPUT);
  // Keep LED on while the sketch is active/awake for bring-up visibility.
  digitalWrite(raven::PIN_LED_GREEN, HIGH);

  if (!node.beginHardware())
  {
    Serial.println(F("Init: beginHardware failed."));
  }
  if (!node.beginI2C())
  {
    Serial.println(F("Init: beginI2C failed."));
  }
  if (!node.rtc().begin())
  {
    Serial.println(F("Init: DS3231 not found."));
  }

  if (!logger.begin())
  {
    Serial.println(F("Init: logger begin failed."));
  }

  Serial.print(F("CSV Header: "));
  Serial.println(raven::DataLoggerHelper::csvHeader());
}

void loop()
{
  raven::SampleFields sample = logger.captureSample();
  String logPath;
  if (raven::resolveLogFilePath(node.sd(), gLogFilePolicy, sample.rtc, logPath) !=
      raven::ServiceStatus::Ok)
  {
    Serial.println(F("Log path: invalid filename policy"));
    delay(5000);
    return;
  }

  String csvLine = raven::DataLoggerHelper::toCsv(sample);

  if (!node.sd().exists(logPath.c_str()))
  {
    if (node.sd().appendLine(logPath.c_str(), raven::DataLoggerHelper::csvHeader()) ==
        raven::ServiceStatus::Ok)
    {
      Serial.print(F("Logged Header: "));
      Serial.println(raven::DataLoggerHelper::csvHeader());
    }
  }

  raven::ServiceStatus logStatus = node.sd().appendLine(logPath.c_str(), csvLine);
  Serial.print(F("Log write: "));
  Serial.println(raven::statusToString(logStatus));
  Serial.print(F("Log path: "));
  Serial.println(logPath);
  Serial.print(F("Logged CSV: "));
  Serial.println(csvLine);

  delay(5000);
}
