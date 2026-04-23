#pragma once

#include "ServiceTypes.h"
#include <Adafruit_VEML7700.h>
#include <Wire.h>

namespace raven {

struct LightReading {
  ServiceStatus status = ServiceStatus::NotInitialized;
  uint16_t als = 0;
  uint16_t white = 0;
  float lux = NAN;
  bool lowThreshold = false;
  bool highThreshold = false;
};

class LightService {
public:
  bool begin(TwoWire &wire = Wire);
  LightReading readSample();
  bool isInitialized() const { return initialized_; }

private:
  TwoWire *wire_ = nullptr;
  Adafruit_VEML7700 sensor_;
  bool initialized_ = false;
};

} // namespace raven
