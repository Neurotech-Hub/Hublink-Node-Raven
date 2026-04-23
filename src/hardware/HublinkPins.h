#pragma once

#include <Arduino.h>

namespace raven {

constexpr uint8_t PIN_AUX_GPIO0 = 1; // MAG_OUT
constexpr uint8_t PIN_AUX_GPIO1 = 2;
constexpr uint8_t PIN_I2C_SDA = 3;
constexpr uint8_t PIN_I2C_SCL = 4;
constexpr uint8_t PIN_GPIO5 = 5;
constexpr uint8_t PIN_GPIO6 = 6;
constexpr uint8_t PIN_I2C_EN = 7; // active-low power gate
constexpr uint8_t PIN_A5 = 8;
constexpr uint8_t PIN_GPIO9 = 9;
constexpr uint8_t PIN_GPIO10 = 10;
constexpr uint8_t PIN_GPIO11 = 11;
constexpr uint8_t PIN_GPIO12 = 12;
constexpr uint8_t PIN_GPIO13 = 13;
constexpr uint8_t PIN_LED_GREEN = PIN_GPIO13;
constexpr uint8_t PIN_A4 = 14;
constexpr uint8_t PIN_A3 = 15;
constexpr uint8_t PIN_A2 = 16;
constexpr uint8_t PIN_A1 = 17;
constexpr uint8_t PIN_A0 = 18;
constexpr uint8_t PIN_ALERT = 21;
constexpr uint8_t PIN_LED_B = 33;
constexpr uint8_t PIN_USB_SENSE = 34;
constexpr uint8_t PIN_SPI_MOSI = 35;
constexpr uint8_t PIN_SPI_SCK = 36;
constexpr uint8_t PIN_SPI_MISO = 37;
constexpr uint8_t PIN_UART_RX = 38;
constexpr uint8_t PIN_UART_TX = 39;
constexpr uint8_t PIN_RTC_PWR = 41;
constexpr uint8_t PIN_TXDO = 43;
constexpr uint8_t PIN_SD_EN = 45; // active-low
constexpr uint8_t PIN_SD_CS = 46;

constexpr uint32_t DEFAULT_I2C_CLOCK_HZ = 100000;
constexpr uint32_t DEFAULT_SD_SPI_CLOCK_HZ = 10000000;

} // namespace raven
