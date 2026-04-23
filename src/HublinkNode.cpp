#include "HublinkNode.h"
#include <esp_log.h>

namespace raven {

bool HublinkNode::beginHardware() {
  pinMode(PIN_AUX_GPIO0, INPUT_PULLUP);
  pinMode(PIN_AUX_GPIO1, INPUT_PULLUP);
  pinMode(PIN_GPIO5, INPUT);
  pinMode(PIN_GPIO6, INPUT);

  pinMode(PIN_I2C_EN, OUTPUT);
  setI2CPowerEnabled(true);

  pinMode(PIN_A5, INPUT);
  pinMode(PIN_GPIO9, INPUT);
  pinMode(PIN_GPIO10, INPUT);
  pinMode(PIN_GPIO11, INPUT);
  pinMode(PIN_GPIO12, INPUT);
  pinMode(PIN_A4, INPUT);
  pinMode(PIN_A3, INPUT);
  pinMode(PIN_A2, INPUT);
  pinMode(PIN_A1, INPUT);
  pinMode(PIN_A0, INPUT);
  pinMode(PIN_ALERT, INPUT);

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setStatusLeds(false);

  pinMode(PIN_USB_SENSE, INPUT_PULLUP);

  pinMode(PIN_SPI_MOSI, INPUT);
  pinMode(PIN_SPI_SCK, INPUT);
  pinMode(PIN_SPI_MISO, INPUT);

  pinMode(PIN_UART_RX, INPUT);
  pinMode(PIN_UART_TX, OUTPUT);
  digitalWrite(PIN_UART_TX, LOW);

  pinMode(PIN_TXDO, OUTPUT);
  digitalWrite(PIN_TXDO, LOW);

  pinMode(PIN_SD_EN, OUTPUT);
  digitalWrite(PIN_SD_EN, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  hardwareInitialized_ = true;
  return true;
}

bool HublinkNode::beginI2C(uint32_t clockHz) {
  if (!hardwareInitialized_) {
    beginHardware();
  }
  // Optional peripherals may NACK when not populated; keep logs quiet by default.
  esp_log_level_set("i2c.master", ESP_LOG_NONE);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(clockHz);
  return true;
}

void HublinkNode::setI2CPowerEnabled(bool enabled) {
  // This hardware uses active-low enable.
  digitalWrite(PIN_I2C_EN, enabled ? LOW : HIGH);
}

bool HublinkNode::isI2CPowerEnabled() const {
  return digitalRead(PIN_I2C_EN) == LOW;
}

bool HublinkNode::readMagnet() const { return digitalRead(PIN_AUX_GPIO0) == HIGH; }

bool HublinkNode::readUsbSense() const {
  return digitalRead(PIN_USB_SENSE) == HIGH;
}

void HublinkNode::setStatusLeds(bool on) {
  const int level = on ? HIGH : LOW;
  digitalWrite(PIN_LED_B, level);
  digitalWrite(PIN_LED_GREEN, level);
}

esp_sleep_wakeup_cause_t HublinkNode::wakeupCause() const {
  return esp_sleep_get_wakeup_cause();
}

bool HublinkNode::isTimerWake() const {
  return wakeupCause() == ESP_SLEEP_WAKEUP_TIMER;
}

} // namespace raven
