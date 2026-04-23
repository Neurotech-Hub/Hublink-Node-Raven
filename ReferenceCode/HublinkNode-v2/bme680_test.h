/*
 * BME680 — gas, humidity, temperature, pressure (I2C).
 * Single-shot read for bring-up cycle; non-blocking on failures.
 */

#pragma once

#include "Adafruit_BME680.h"
#include <Adafruit_Sensor.h>
#include <Arduino.h>

static constexpr float kSeaLevelPressureHpa = 1013.25f;

inline Adafruit_BME680 &bme680() {
  static Adafruit_BME680 dev(&Wire);
  return dev;
}

inline float bme680AltitudeFromPressureHpa(float pressureHpa, float seaLevelHpa) {
  return 44330.0f * (1.0f - powf(pressureHpa / seaLevelHpa, 0.1903f));
}

inline bool bme680InitOnce() {
  static bool inited = false;
  if (inited) {
    return true;
  }

  bool ok = bme680().begin(0x77, true);
  if (!ok) {
    ok = bme680().begin(0x76, true);
  }
  if (!ok) {
    return false;
  }

  // Configure once. Each performReading() triggers a single forced conversion
  // and the sensor returns to sleep afterward (one-shot behavior).
  bme680().setTemperatureOversampling(BME680_OS_8X);
  bme680().setHumidityOversampling(BME680_OS_2X);
  bme680().setPressureOversampling(BME680_OS_4X);
  bme680().setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme680().setGasHeater(320, 150);

  inited = true;
  return true;
}

inline void bme680TestOnce() {
  Serial.println(F("\n--- BME680 ---"));

  if (!bme680InitOnce()) {
    Serial.println(F("BME680: not found (skip). Check wiring, I2C_EN, addr 0x76/0x77."));
    return;
  }

  if (!bme680().performReading()) {
    Serial.println(F("BME680: reading failed (skip)."));
    return;
  }
  float pressureHpa = bme680().pressure / 100.0f;

  Serial.print(F("Temperature: "));
  Serial.print(bme680().temperature);
  Serial.println(F(" C"));

  Serial.print(F("Pressure: "));
  Serial.print(pressureHpa);
  Serial.println(F(" hPa"));

  Serial.print(F("Humidity: "));
  Serial.print(bme680().humidity);
  Serial.println(F(" %"));

  Serial.print(F("Gas: "));
  Serial.print(bme680().gas_resistance / 1000.0f);
  Serial.println(F(" KOhms"));

  Serial.print(F("Approx. Altitude: "));
  Serial.print(bme680AltitudeFromPressureHpa(pressureHpa, kSeaLevelPressureHpa));
  Serial.println(F(" m"));
}
