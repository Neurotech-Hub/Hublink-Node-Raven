/*
 * Hublink Node v2 — ESP32-S3 GPIO map (custom hardware).
 * GPIO0 = BOOT (strap); not configured here.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

// --- GPIO aliases (comment = alternate names) ---

constexpr uint8_t PIN_AUX_GPIO0 = 1; // MAG_OUT
constexpr uint8_t PIN_AUX_GPIO1 = 2; // schematic “AUX_GPIO1” (not chip GPIO0)
constexpr uint8_t PIN_I2C_SDA = 3;
constexpr uint8_t PIN_I2C_SCL = 4;
constexpr uint8_t PIN_GPIO5 = 5;
constexpr uint8_t PIN_GPIO6 = 6;
constexpr uint8_t PIN_I2C_EN = 7;
constexpr uint8_t PIN_A5 = 8; // GPIO8
constexpr uint8_t PIN_GPIO9 = 9;
constexpr uint8_t PIN_GPIO10 = 10;
constexpr uint8_t PIN_GPIO11 = 11;
constexpr uint8_t PIN_GPIO12 = 12;
constexpr uint8_t PIN_GPIO13 = 13;
/** Red status LED (hardware on GPIO13). Same net as PIN_GPIO13. */
constexpr uint8_t PIN_LED_RED = PIN_GPIO13;
constexpr uint8_t PIN_A4 = 14; // GPIO14
constexpr uint8_t PIN_A3 = 15; // GPIO15
constexpr uint8_t PIN_A2 = 16; // GPIO16
constexpr uint8_t PIN_A1 = 17; // GPIO17
constexpr uint8_t PIN_A0 = 18; // GPIO18
constexpr uint8_t PIN_ALERT = 21;
constexpr uint8_t PIN_LED_B = 33;
constexpr uint8_t PIN_USB_SENSE = 34;
constexpr uint8_t PIN_SPI_MOSI = 35;
constexpr uint8_t PIN_SPI_SCK = 36;
constexpr uint8_t PIN_SPI_MISO = 37;
constexpr uint8_t PIN_UART_RX = 38; // GPIO38
constexpr uint8_t PIN_UART_TX = 39; // GPIO39
constexpr uint8_t PIN_RTC_PWR = 41;
constexpr uint8_t PIN_TXDO = 43;  // GPIO43
constexpr uint8_t PIN_SD_EN = 45; // GPIO45
constexpr uint8_t PIN_SD_CS = 46; // GPIO46

/** Default pin modes and levels for bring-up. */
inline void configureBoardPins()
{
  pinMode(PIN_AUX_GPIO0, INPUT_PULLUP);
  pinMode(PIN_AUX_GPIO1, INPUT_PULLUP);

  pinMode(PIN_GPIO5, INPUT);
  pinMode(PIN_GPIO6, INPUT);
  pinMode(PIN_I2C_EN, OUTPUT);
  digitalWrite(PIN_I2C_EN, LOW); // ACTIVE LOW

  pinMode(PIN_A5, INPUT);
  pinMode(PIN_GPIO9, INPUT);
  pinMode(PIN_GPIO10, INPUT);
  pinMode(PIN_GPIO11, INPUT);
  pinMode(PIN_GPIO12, INPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_RED, LOW);
  pinMode(PIN_A4, INPUT);
  pinMode(PIN_A3, INPUT);
  pinMode(PIN_A2, INPUT);
  pinMode(PIN_A1, INPUT);
  pinMode(PIN_A0, INPUT);

  pinMode(PIN_ALERT, INPUT);

  pinMode(PIN_LED_B, OUTPUT);
  digitalWrite(PIN_LED_B, LOW);

  pinMode(PIN_USB_SENSE, INPUT_PULLUP);

  // SPI pins: leave for SPI.begin(SCK, MISO, MOSI, SS) when you test SPI.
  pinMode(PIN_SPI_MOSI, INPUT);
  pinMode(PIN_SPI_SCK, INPUT);
  pinMode(PIN_SPI_MISO, INPUT);

  pinMode(PIN_UART_RX, INPUT);
  pinMode(PIN_UART_TX, OUTPUT);
  digitalWrite(PIN_UART_TX, LOW);

  pinMode(PIN_TXDO, OUTPUT);
  digitalWrite(PIN_TXDO, LOW);

  pinMode(PIN_SD_EN, OUTPUT);
  digitalWrite(PIN_SD_EN, HIGH); // ACTIVE LOW (default off)
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  // Start conservative for bring-up; long traces/weak pull-ups fail at 400 kHz.
  Wire.setClock(100000);
}
