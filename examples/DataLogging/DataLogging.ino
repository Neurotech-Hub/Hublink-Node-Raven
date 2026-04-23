#include <HublinkNodeRaven.h>
#include <sys/time.h>
#include <time.h>

raven::HublinkNode node;
raven::DataLoggerHelper logger(node);

const char *kLogPath = "/hublink_log.csv";
bool wroteHeader = false;

static bool syncSystemClockFromRtc() {
  raven::RtcReading rtc = node.rtc().readSample();
  if (rtc.status != raven::ServiceStatus::Ok || !rtc.now.isValid()) {
    Serial.println(F("RTC sync: failed (RTC unavailable/invalid)."));
    return false;
  }

  const time_t epoch = static_cast<time_t>(rtc.now.unixtime());
  // Guard against obviously bad RTC values before applying system time.
  if (epoch < 1700000000 || epoch > 2200000000) {
    Serial.println(F("RTC sync: rejected (epoch out of safe range)."));
    return false;
  }

  struct timeval tv = {};
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    Serial.println(F("RTC sync: settimeofday failed."));
    return false;
  }

  time_t nowEpoch = time(nullptr);
  Serial.print(F("RTC sync: system epoch set to "));
  Serial.println(static_cast<uint32_t>(nowEpoch));
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(raven::PIN_LED_GREEN, OUTPUT);
  // Keep LED on while the sketch is active/awake for bring-up visibility.
  digitalWrite(raven::PIN_LED_GREEN, HIGH);

  if (!node.beginHardware()) {
    Serial.println(F("Init: beginHardware failed."));
  }
  if (!node.beginI2C()) {
    Serial.println(F("Init: beginI2C failed."));
  }
  if (!node.rtc().begin()) {
    Serial.println(F("Init: DS3231 not found."));
  }

  syncSystemClockFromRtc();

  if (!logger.begin()) {
    Serial.println(F("Init: logger begin failed."));
  }

  Serial.print(F("CSV Header: "));
  Serial.println(raven::DataLoggerHelper::csvHeader());
}

void loop() {
  raven::CompositeSample sample = logger.captureSample();
  String csvLine = raven::DataLoggerHelper::toCsv(sample);

  if (!wroteHeader) {
    if (node.sd().appendLine(kLogPath, raven::DataLoggerHelper::csvHeader()) ==
        raven::ServiceStatus::Ok) {
      wroteHeader = true;
      Serial.print(F("Logged Header: "));
      Serial.println(raven::DataLoggerHelper::csvHeader());
    }
  }

  raven::ServiceStatus logStatus = node.sd().appendLine(kLogPath, csvLine);
  Serial.print(F("Log write: "));
  Serial.println(raven::statusToString(logStatus));
  Serial.print(F("Logged CSV: "));
  Serial.println(csvLine);

  delay(5000);
}
