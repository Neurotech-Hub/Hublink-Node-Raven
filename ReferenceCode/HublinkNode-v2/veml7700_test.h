/*
 * VEML7700 ambient light (I2C). Wire must be started first — see configureBoardPins().
 */

#pragma once

#include "Adafruit_VEML7700.h"
#include <Arduino.h>

inline Adafruit_VEML7700 &veml7700() {
  static Adafruit_VEML7700 dev;
  return dev;
}

inline bool veml7700InitOnce() {
  static bool inited = false;
  if (inited) {
    return true;
  }
  if (!veml7700().begin(&Wire)) {
    return false;
  }

  veml7700().setLowThreshold(10000);
  veml7700().setHighThreshold(20000);
  veml7700().interruptEnable(true);

  // Keep low idle draw between one-shot reads.
  veml7700().powerSaveEnable(true);
  veml7700().enable(false);

  inited = true;
  return true;
}

/** Single attempt: init + one sample, or short message and return. */
inline void veml7700TestOnce() {
  Serial.println(F("\n--- VEML7700 ---"));
  if (!veml7700InitOnce()) {
    Serial.println(F("VEML7700: not found (skip)."));
    return;
  }

  // One-shot: enable, take one integration window, then shutdown again.
  veml7700().enable(true);
  uint16_t als = veml7700().readALS(true);
  uint16_t white = veml7700().readWhite(false);
  float lux = veml7700().readLux(VEML_LUX_NORMAL_NOWAIT);
  uint16_t irq = veml7700().interruptStatus();
  veml7700().enable(false);

  Serial.print(F("ALS: "));
  Serial.print(als);
  Serial.print(F("  white: "));
  Serial.print(white);
  Serial.print(F("  lux: "));
  Serial.println(lux);

  if (irq & VEML7700_INTERRUPT_LOW) {
    Serial.println(F("VEML7700: low threshold irq"));
  }
  if (irq & VEML7700_INTERRUPT_HIGH) {
    Serial.println(F("VEML7700: high threshold irq"));
  }
}
