#pragma once

#include "../hardware/HublinkPins.h"
#include "ServiceTypes.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>

namespace raven {

class SdService {
public:
  bool begin(uint32_t spiClockHz = DEFAULT_SD_SPI_CLOCK_HZ);
  void end();
  bool isMounted() const { return mounted_; }
  uint8_t cardType() const;
  uint64_t cardSizeBytes() const;

  ServiceStatus appendLine(const char *path, const String &line);
  ServiceStatus readText(const char *path, String &outText);
  ServiceStatus remove(const char *path);

private:
  bool mount(uint32_t spiClockHz);
  bool mounted_ = false;
};

} // namespace raven
