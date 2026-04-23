/*
 * Hublink Node v2 — bring-up / per-module tests
 *
 * Board: ESP32-S3 Dev Module (or match your flash/PSRAM options).
 * Pin map: see pins.h
 *
 * One full test cycle at startup with timing markers, then timed deep sleep.
 */

#include "board_tests.h"
#include "esp_log.h"
#include "esp_sleep.h"

RTC_DATA_ATTR bool gRanStartupSleepCycle = false;

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {
    delay(10);
  }

  Serial.println(F("Hublink Node v2"));
  // Suppress noisy ESP-IDF I2C driver error logs during optional sensor checks.
  esp_log_level_set("i2c.master", ESP_LOG_NONE);

  configureBoardPins();

  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println(F("Wake: timer"));
    delay(500);
    if (initSdCardMounted()) {
      Serial.println(F("SD re-init after wake: OK"));
    } else {
      Serial.println(F("SD re-init after wake: failed"));
    }
    gRanStartupSleepCycle = true;
    return;
  }

  // Run one full serial test pass at startup.
  runFullTestCycle();

  if (!gRanStartupSleepCycle) {
    gRanStartupSleepCycle = true;
    Serial.println(F("Preparing deep sleep: SD off, sleep 3s"));
    digitalWrite(PIN_SD_EN, HIGH); // active-low SD enable: HIGH = off
    delay(20);
    esp_sleep_enable_timer_wakeup(3000000ULL);
    esp_deep_sleep_start();
  }

  // Example: hardware UART on GPIO38/39 when you need it (not USB Serial).
  // Serial1.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
}

void loop() {
  // Both LEDs: inverted MAG_OUT (magnet active-high -> LEDs on when line is low).
  bool mag = digitalRead(PIN_AUX_GPIO0) == HIGH;
  int led = mag ? LOW : HIGH;
  digitalWrite(PIN_LED_B, led);
  digitalWrite(PIN_LED_RED, led);
  delay(1);
}
