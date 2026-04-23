/*
 * SD card on SPI (PIN_SD_EN powers/enables the slot; PIN_SD_CS = SPI CS).
 * This board uses active-low SD enable: LOW=enabled, HIGH=disabled.
 */

#pragma once

#include "pins.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>

/** SPI clock for SD (Hz). Try 4000000 if mount is flaky. */
constexpr uint32_t kSdSpiFreq = 10000000;

inline bool initSdCardMounted() {
  digitalWrite(PIN_SD_EN, LOW);
  delay(50);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  if (!SD.begin(PIN_SD_CS, SPI, kSdSpiFreq)) {
    SPI.end();
    digitalWrite(PIN_SD_EN, HIGH);
    return false;
  }
  return true;
}

/**
 * Enables SD (PIN_SD_EN), starts SPI on shared bus pins, mounts FAT.
 * Prints diagnostics to Serial. Returns true if mounted.
 */
inline bool runSdCardTest() {
  Serial.println(F("\n--- SD (SPI) ---"));
  if (!initSdCardMounted()) {
    Serial.println(F("SD mount failed (skip)."));
    return false;
  }

  uint8_t t = SD.cardType();
  Serial.print("Card type: ");
  if (t == CARD_NONE) {
    Serial.println("none");
  } else if (t == CARD_MMC) {
    Serial.println("MMC");
  } else if (t == CARD_SD) {
    Serial.println("SDSC");
  } else if (t == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("unknown");
  }

  uint64_t sizeBytes = SD.cardSize();
  Serial.print("Card size: ");
  Serial.print((double)sizeBytes / (1024.0 * 1024.0), 1);
  Serial.println(" MB");

  const char *path = "/sd_test.txt";
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.println(F("SD open write failed (skip)."));
    SD.end();
    SPI.end();
    digitalWrite(PIN_SD_EN, HIGH);
    return false;
  }
  f.println("Hublink Node v2 SD OK");
  f.close();

  f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.println(F("SD open read failed (skip)."));
    SD.end();
    SPI.end();
    digitalWrite(PIN_SD_EN, HIGH);
    return false;
  }
  Serial.print("Read back: ");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();

  SD.remove(path);
  Serial.println(F("SD test OK."));
  SD.end();
  SPI.end();
  digitalWrite(PIN_SD_EN, HIGH);
  return true;
}
