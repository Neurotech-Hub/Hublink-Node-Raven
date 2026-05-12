#include <HublinkNodeRaven.h>

raven::HublinkNode node;

// false: you poll and call safeguardShutdown() yourself. true: library default (USB-aware sleep).
static constexpr bool kAutomaticSafeguard = false;

void setup() {
  Serial.begin(115200);
  // `beginHardware()` sets the ESP32-S3 CPU to 80 MHz first (stable default for Wi‑Fi / Bluetooth).
  // For maximum performance instead, use e.g. `node.beginHardware(240)` or call
  // `raven::HublinkNode::setMcuClockMhz(240)` after begin and before heavy work.
  node.beginHardware();
  Serial.print(F("MCU clock MHz: "));
  Serial.println(raven::HublinkNode::mcuClockMhz());
  node.beginI2C();
  node.powerGauge().begin();
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

  if (nowMs - lastDiagnoseMs >= 10000U) {
    lastDiagnoseMs = nowMs;
    (void)raven::diagnoseVoltageSafeguard(Serial, node, node.readUsbSense());
  }

  delay(1);
}
