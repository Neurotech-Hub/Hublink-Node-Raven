#include <HublinkNodeRaven.h>

raven::HublinkNode node;

static const __FlashStringHelper *sdTypeName(uint8_t type) {
  switch (type) {
  case CARD_MMC:
    return F("MMC");
  case CARD_SD:
    return F("SDSC");
  case CARD_SDHC:
    return F("SDHC");
  default:
    return F("unknown");
  }
}

void setup() {
  Serial.begin(115200);
  node.beginHardware();
  node.beginI2C();

  node.rtc().begin();
  node.powerGauge().begin();
  node.light().begin();
  node.environment().begin();
}

void loop() {
  raven::RtcReading clockReading = node.rtc().readSample();
  raven::BatteryReading batteryReading = node.powerGauge().readSample();
  raven::LightReading lightReading = node.light().readSample();
  raven::EnvReading environmentReading = node.environment().readSample();
  const bool isMagnetDetected = node.readMagnet();

  const bool sdMounted = node.sd().begin();
  const uint8_t sdType = node.sd().cardType();
  const uint64_t sdSize = node.sd().cardSizeBytes();

  Serial.println();
  Serial.println(F("--------- SensorSnapshot Cycle --------"));
  Serial.println(F("========== Raven Snapshot ============="));
  if (clockReading.status == raven::ServiceStatus::Ok) {
    char datetime[24];
    snprintf(datetime, sizeof(datetime), "%04d-%02d-%02d %02d:%02d:%02d",
             clockReading.now.year(), clockReading.now.month(), clockReading.now.day(), clockReading.now.hour(),
             clockReading.now.minute(), clockReading.now.second());
    Serial.print(F("Time     : "));
    Serial.print(datetime);
    Serial.print(F("  | unix "));
    Serial.println(clockReading.now.unixtime());
  } else {
    Serial.println(F("Time     : unavailable"));
  }

  if (batteryReading.status == raven::ServiceStatus::Ok && batteryReading.hasCellReading) {
    Serial.print(F("Battery  : "));
    Serial.print(batteryReading.voltageV, 3);
    Serial.print(F(" V  | "));
    Serial.print(batteryReading.stateOfChargePct, 1);
    Serial.println(F(" %"));
  } else {
    Serial.println(F("Battery  : not present / no valid cell reading"));
  }

  if (lightReading.status == raven::ServiceStatus::Ok) {
    Serial.print(F("Light    : lux "));
    Serial.print(lightReading.lux, 2);
    Serial.print(F("  | als "));
    Serial.print(lightReading.als);
    Serial.print(F("  | white "));
    Serial.println(lightReading.white);
  } else {
    Serial.println(F("Light    : unavailable"));
  }

  if (environmentReading.status == raven::ServiceStatus::Ok) {
    Serial.print(F("Env      : temp "));
    Serial.print(environmentReading.temperatureC, 2);
    Serial.print(F(" C  | rh "));
    Serial.print(environmentReading.humidityPct, 2);
    Serial.print(F(" %  | pressure "));
    Serial.print(environmentReading.pressureHpa, 2);
    Serial.print(F(" hPa"));
    Serial.println();

    Serial.print(F("           gas "));
    Serial.print(environmentReading.gasKOhms, 2);
    Serial.print(F(" kOhm  | altitude "));
    Serial.println(environmentReading.altitudeM, 2);
  } else {
    Serial.println(F("Env      : unavailable"));
  }

  Serial.print(F("AUX      : GPIO0(MAG_OUT) "));
  Serial.print(digitalRead(raven::PIN_AUX_GPIO0) == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F("  |  GPIO1 "));
  Serial.println(digitalRead(raven::PIN_AUX_GPIO1) == HIGH ? F("HIGH") : F("LOW"));

  Serial.print(F("Magnet   : "));
  Serial.print(isMagnetDetected ? F("HIGH") : F("LOW"));
  Serial.print(F("  | ulp edges "));
  Serial.print(node.magnetCounter().edgeCount());
  Serial.print(F("  | passes "));
  Serial.println(node.magnetCounter().magnetPassCount());

  if (sdMounted && sdType != CARD_NONE) {
    Serial.print(F("SD       : present  | type "));
    Serial.print(sdTypeName(sdType));
    Serial.print(F("  | size "));
    Serial.println((double)sdSize / (1024.0 * 1024.0), 1);
  } else {
    Serial.println(F("SD       : not present"));
  }
  Serial.println(F("======================================"));
  Serial.println(F("--------------------------------------"));

  delay(3000);
}
