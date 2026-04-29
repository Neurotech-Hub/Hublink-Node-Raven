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

  Serial.println();
  Serial.println(F("--------- DataLogging Active -----------"));
  Serial.print(F("CSV Header: "));
  Serial.println(raven::DataLoggerHelper::csvHeader());
  Serial.println(F("----------------------------------------"));
}

void loop()
{
  Serial.println();
  Serial.println(F("--------- DataLogging Cycle ------------"));
  raven::SampleFields sample;
  String logPath;
  raven::ServiceStatus logStatus = raven::captureAndAppendManagedCsv(
      logger, node, gLogFilePolicy, 0, sample, &logPath);
  if (logStatus != raven::ServiceStatus::Ok)
  {
    Serial.println(F("Log write failed"));
    delay(5000);
    return;
  }

  String csvLine = raven::DataLoggerHelper::toCsv(sample);
  Serial.print(F("Log write: "));
  Serial.println(raven::statusToString(logStatus));
  Serial.print(F("Log path: "));
  Serial.println(logPath);
  Serial.print(F("Logged CSV: "));
  Serial.println(csvLine);
  Serial.println(F("----------------------------------------"));

  delay(5000);
}
