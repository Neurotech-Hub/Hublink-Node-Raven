/*
 * MAX17048 fuel gauge (I2C). Wire must be started first — see configureBoardPins().
 *
 * Important: Adafruit_MAX17048::begin() performs a chip reset. We only call it
 * once; calling begin() every test cycle was clearing readings (0 V / 0 %).
 */

#pragma once

#include "Adafruit_MAX1704X.h"
#include <Arduino.h>

inline Adafruit_MAX17048 &max17048() {
  static Adafruit_MAX17048 dev;
  return dev;
}

inline void max17048WakeForRead() {
  // Library exposes explicit hibernate/wake controls.
  max17048().wake();
  delay(3);
}

inline void max17048HibernateAfterRead() {
  max17048().hibernate();
}

/** Single attempt: init once, then read each cycle. */
inline void max17048TestOnce() {
  Serial.println(F("\n--- MAX17048 ---"));

  static bool inited = false;
  if (!inited) {
    if (!max17048().begin(&Wire)) {
      Serial.println(F("MAX17048: not found (skip)."));
      return;
    }
    inited = true;
    // Keep init read-only after begin(); quickStart writes can NACK on some boards
    // during bring-up even when subsequent reads are valid.
    delay(200);

    Serial.print(F("MAX17048 OK, Chip ID: 0x"));
    Serial.println(max17048().getChipID(), HEX);

    // Idle in hibernate between measurement windows.
    max17048HibernateAfterRead();
  }

  max17048WakeForRead();
  float v = max17048().cellVoltage();
  if (isnan(v)) {
    Serial.println(F("MAX17048: not ready / voltage unavailable (skip)."));
    max17048HibernateAfterRead();
    return;
  }
  float soc = max17048().cellPercent();
  max17048HibernateAfterRead();

  Serial.print(F("Batt: "));
  Serial.print(v, 3);
  Serial.print(F(" V, "));
  Serial.print(soc, 1);
  Serial.println(F(" %"));

  if (v < 0.05f) {
    Serial.println(
        F("Note: 0 V on VCELL usually means pack +/- is not on the MAX17048 "
          "cell sense pins (I2C alone is not enough)."));
  }
}
