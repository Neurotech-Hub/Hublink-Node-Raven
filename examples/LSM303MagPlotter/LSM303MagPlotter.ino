// LSM303MagPlotter — LSM303AGR magnetometer raw XYZ over I2C → Arduino Serial Plotter.
//
// Magnetometer I2C address: 0x1E (7-bit). WHO_AM_I expected: 0x40.
// Configuration: HR mode, 100 Hz ODR, BDU enabled, continuous mode, raw 16-bit reads.
//
// Uses a minimal local I2C helper (Lsm303Mag.h) instead of STM32duino LSM303AGR, which
// conflicts with ESP32 lwIP type definitions (u32_t) when its .cpp files are compiled.
//
// Usage: upload, open Tools → Serial Plotter at 115200 baud. Do not use Serial Monitor
// after boot — debug text after the label line breaks plotting.

#include "Lsm303Mag.h"
#include <HublinkNodeRaven.h>

raven::HublinkNode node;
Lsm303Mag mag;

static constexpr uint32_t kSampleIntervalMs = 200;

static void blinkErrorForever() {
  while (true) {
    node.setStatusLeds(true);
    delay(200);
    node.setStatusLeds(false);
    delay(200);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  node.beginHardware();
  node.beginI2C();
  delay(50);

  if (!mag.begin(Wire)) {
    Serial.println(F("LSM303MagPlotter: magnetometer init failed"));
    blinkErrorForever();
  }

  Serial.println(F("MagX\tMagY\tMagZ"));
}

void loop() {
  int16_t raw[3];

  if (mag.readRaw(raw)) {
    Serial.print(raw[0]);
    Serial.print('\t');
    Serial.print(raw[1]);
    Serial.print('\t');
    Serial.println(raw[2]);
  }

  delay(kSampleIntervalMs);
}
