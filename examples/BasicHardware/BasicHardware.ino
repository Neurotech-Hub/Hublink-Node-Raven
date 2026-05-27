#include <HublinkNodeRaven.h>
#include <Wire.h>

raven::HublinkNode node;

// false: you poll and call safeguardShutdown() yourself. true: library default (USB-aware sleep).
static constexpr bool kAutomaticSafeguard = false;

/// Scan the active `Wire` bus and print 7-bit addresses that ACK, then probe the I2C General Call
/// address (0x00). Safe to call repeatedly.
///
/// Notes on 10-bit slaves (e.g. Zilog ZDP323B1..B4 at 0x301..0x304):
/// Arduino `Wire` on ESP32 only emits 7-bit slave addresses, so 10-bit devices cannot be reached
/// with `Wire.beginTransmission(addr)` and will not appear in the 7-bit list below. The ZDP323B
/// also responds to the I2C **General Call** address (0x00, broadcast); a General Call ACK only
/// confirms that **at least one** General-Call-capable device is on the bus — it does not uniquely
/// identify the ZDP323B. Proper 10-bit transactions require the ESP-IDF `i2c_master` driver.
static void scanAndPrintI2c(Stream &out) {
  out.println(F("--------- I2C scan -------------------"));
  uint8_t found = 0;
  for (uint8_t addr = 0x01; addr < 0x78; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      out.print(F("  0x"));
      if (addr < 0x10) {
        out.print('0');
      }
      out.println(addr, HEX);
      ++found;
    }
  }
  if (found == 0) {
    out.println(F("  (no 7-bit devices ACK'd)"));
  } else {
    out.print(F("  total="));
    out.println(found);
  }

  Wire.beginTransmission(static_cast<uint8_t>(0x00));
  const uint8_t gcStatus = Wire.endTransmission();
  out.print(F("  General Call (0x00): "));
  if (gcStatus == 0) {
    out.println(F("ACK (>=1 GC-capable device present, e.g. ZDP323B)"));
  } else {
    out.println(F("NACK"));
  }
}

void setup() {
  Serial.begin(115200);
  // `beginHardware()` sets the ESP32-S3 CPU to 80 MHz first (stable default for Wi‑Fi / Bluetooth).
  // For maximum performance instead, use e.g. `node.beginHardware(240)` or call
  // `raven::HublinkNode::setMcuClockMhz(240)` after begin and before heavy work.
  node.beginHardware();
  Serial.print(F("MCU clock MHz: "));
  Serial.println(raven::HublinkNode::mcuClockMhz());
  // Aux I2C power gate is active-low; beginHardware() already drove PIN_I2C_EN LOW, but make it
  // explicit so this example documents the requirement before any I2C scan / sensor probe.
  node.setI2CPowerEnabled(true);
  node.beginI2C();
  delay(50); // let sensors finish power-on before scanning
  node.powerGauge().begin();
  scanAndPrintI2c(Serial);
}

void loop() {
  const bool magnetHigh = node.readMagnet();
  // Boot switch (active LOW): force green LED on; otherwise mirror magnet on both status LEDs.
  if (digitalRead(raven::PIN_BOOT_BUTTON) == LOW) {
    digitalWrite(raven::PIN_LED_GREEN, HIGH);
  } else {
    node.setStatusLeds(!magnetHigh);
  }

  static uint32_t lastPrintMs = 0;
  static uint32_t lastDiagnoseMs = 0;
  static uint32_t lastSafeguardMs = 0;
  const uint32_t nowMs = millis();

  constexpr uint32_t kPollMs = raven::kSafeguardPollIntervalSecondsDefault * 1000UL;
  if (lastSafeguardMs == 0U || static_cast<uint32_t>(nowMs - lastSafeguardMs) >= kPollMs) {
    lastSafeguardMs = nowMs;
    if (kAutomaticSafeguard) {
      raven::maybeAutomaticVoltageSafeguard(node, true);
    } else if (raven::isCellBelowTripVoltage(node) && !node.readUsbSense()) {
      raven::safeguardShutdown(node, raven::kSafeguardShutdownWakeupSecondsDefault);
    }
  }

  if (nowMs - lastPrintMs >= 100U) {
    lastPrintMs = nowMs;
    Serial.println(F("--------- BasicHardware --------------"));
    Serial.print(F("MAG_OUT="));
    Serial.print(magnetHigh ? F("HIGH") : F("LOW"));
    Serial.print(F(" USB_SENSE="));
    Serial.print(node.readUsbSense() ? F("HIGH") : F("LOW"));
    Serial.print(F(" BOOT="));
    Serial.println(digitalRead(raven::PIN_BOOT_BUTTON) == LOW ? F("LOW(held)") : F("HIGH"));
  }

  if (nowMs - lastDiagnoseMs >= 1000U) {
    lastDiagnoseMs = nowMs;
    (void)raven::diagnoseVoltageSafeguard(Serial, node, node.readUsbSense());
    scanAndPrintI2c(Serial);
  }

  delay(1);
}
