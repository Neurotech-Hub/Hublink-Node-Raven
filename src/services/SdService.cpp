#include "SdService.h"

namespace raven {

bool SdService::mount(uint32_t spiClockHz) {
  digitalWrite(PIN_SD_EN, LOW);
  delay(50);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  mounted_ = SD.begin(PIN_SD_CS, SPI, spiClockHz);
  if (!mounted_) {
    SPI.end();
    digitalWrite(PIN_SD_EN, HIGH);
  }
  return mounted_;
}

bool SdService::begin(uint32_t spiClockHz) {
  if (mounted_) {
    return true;
  }
  return mount(spiClockHz);
}

uint8_t SdService::cardType() const {
  if (!mounted_) {
    return CARD_NONE;
  }
  return SD.cardType();
}

uint64_t SdService::cardSizeBytes() const {
  if (!mounted_) {
    return 0;
  }
  return SD.cardSize();
}

void SdService::end() {
  if (mounted_) {
    SD.end();
  }
  SPI.end();
  digitalWrite(PIN_SD_EN, HIGH);
  mounted_ = false;
}

ServiceStatus SdService::appendLine(const char *path, const String &line) {
  if (!mounted_ && !begin()) {
    return ServiceStatus::NotFound;
  }

  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    return ServiceStatus::WriteFailed;
  }

  if (!f.println(line)) {
    f.close();
    return ServiceStatus::WriteFailed;
  }
  f.close();
  return ServiceStatus::Ok;
}

ServiceStatus SdService::readText(const char *path, String &outText) {
  outText = "";
  if (!mounted_ && !begin()) {
    return ServiceStatus::NotFound;
  }

  File f = SD.open(path, FILE_READ);
  if (!f) {
    return ServiceStatus::ReadFailed;
  }
  while (f.available()) {
    outText += static_cast<char>(f.read());
  }
  f.close();
  return ServiceStatus::Ok;
}

ServiceStatus SdService::remove(const char *path) {
  if (!mounted_ && !begin()) {
    return ServiceStatus::NotFound;
  }
  if (!SD.remove(path)) {
    return ServiceStatus::WriteFailed;
  }
  return ServiceStatus::Ok;
}

} // namespace raven
