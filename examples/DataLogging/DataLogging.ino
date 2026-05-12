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

// This sketch stays awake (no deep sleep / ULP counting), so omit ulp_edges, magnet_passes,
// and passes_min; live GPIO magnet state is enough.
static constexpr raven::CsvFieldMask kCsvFieldMask = raven::csvFields({
    raven::CsvField::Millis,
    raven::CsvField::RtcUnix,
    raven::CsvField::DateTime,
    raven::CsvField::RtcTempC,
    raven::CsvField::BattV,
    raven::CsvField::BattPer,
    raven::CsvField::Lux,
    raven::CsvField::Als,
    raven::CsvField::White,
    raven::CsvField::TempC,
    raven::CsvField::PressureHpa,
    raven::CsvField::HumidityPct,
    raven::CsvField::GasKOhm,
    raven::CsvField::AltM,
    raven::CsvField::Magnet,
    raven::CsvField::UsbSense,
});

static void blinkMissingSdCard()
{
  Serial.println(F("DataLogging: SD card not present. Halting."));
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
  if (!node.sd().begin() || node.sd().cardType() == CARD_NONE)
  {
    blinkMissingSdCard();
  }

  Serial.println();
  Serial.println(F("--------- DataLogging Active -----------"));
  Serial.print(F("CSV Header: "));
  Serial.println(raven::DataLoggerHelper::csvHeader(kCsvFieldMask));
  Serial.println(F("----------------------------------------"));
}

void loop()
{
  Serial.println();
  Serial.println(F("--------- DataLogging Cycle ------------"));
  raven::SampleFields sample;
  String logPath;
  raven::ServiceStatus logStatus = raven::captureAndAppendManagedCsv(
      logger, node, gLogFilePolicy, kCsvFieldMask, sample, &logPath);
  if (logStatus != raven::ServiceStatus::Ok)
  {
    Serial.println(F("Log write failed"));
    delay(5000);
    return;
  }

  String csvLine = raven::DataLoggerHelper::toCsv(sample, kCsvFieldMask);
  Serial.print(F("Log write: "));
  Serial.println(raven::statusToString(logStatus));
  Serial.print(F("Log path: "));
  Serial.println(logPath);
  Serial.print(F("Logged CSV: "));
  Serial.println(csvLine);
  Serial.println(F("----------------------------------------"));

  delay(5000);
}
