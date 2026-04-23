/*
 * One full bring-up cycle: each test tries once, no blocking retries.
 * Call from loop every ~3 s; keep LED / other non-blocking work separate.
 */

#pragma once

#include "bme680_test.h"
#include "ds3231_test.h"
#include "max17048_test.h"
#include "pins.h"
#include "sd_test.h"
#include "veml7700_test.h"
#include <Arduino.h>

constexpr uint32_t kTestCyclePeriodMs = 3000;
constexpr uint32_t kStepDelayMs = 500;

inline void blinkTestMarker() {
  digitalWrite(PIN_LED_RED, HIGH);
  delay(60);
  digitalWrite(PIN_LED_RED, LOW);
}

inline void runGpioSenseSample() {
  Serial.println(F("\n--- GPIO sense ---"));
  Serial.print(F("MAG_OUT (GPIO "));
  Serial.print((int)PIN_AUX_GPIO0);
  Serial.print(F("): "));
  Serial.println(digitalRead(PIN_AUX_GPIO0) == HIGH ? F("HIGH") : F("LOW"));
  Serial.print(F("USB_SENSE (GPIO "));
  Serial.print((int)PIN_USB_SENSE);
  Serial.print(F("): "));
  Serial.println(digitalRead(PIN_USB_SENSE) == HIGH ? F("HIGH") : F("LOW"));
}

inline void runRtcTestWithI2cPowerGate() {
  // I2C_EN is active-low on this board: only power RTC rail during RTC access.
  digitalWrite(PIN_I2C_EN, LOW);
  delay(5);
  ds3231TestOnce();
  digitalWrite(PIN_I2C_EN, HIGH);
}

inline void runFullTestCycle() {
  Serial.println(F("\n========== test cycle =========="));
  blinkTestMarker();
  runSdCardTest();
  delay(kStepDelayMs);

  blinkTestMarker();
  max17048TestOnce();
  delay(kStepDelayMs);

  blinkTestMarker();
  veml7700TestOnce();
  delay(kStepDelayMs);

  blinkTestMarker();
  bme680TestOnce();
  delay(kStepDelayMs);

  blinkTestMarker();
  runRtcTestWithI2cPowerGate();
  delay(kStepDelayMs);

  blinkTestMarker();
  runGpioSenseSample();
  delay(kStepDelayMs);
  Serial.println(F("========== end cycle =========="));
}
