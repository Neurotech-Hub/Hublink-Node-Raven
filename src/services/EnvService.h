#pragma once

#include "ServiceTypes.h"
#include <Adafruit_BME680.h>
#include <Wire.h>

namespace raven {

struct EnvReading {
  ServiceStatus status = ServiceStatus::NotInitialized;
  float temperatureC = NAN;
  float pressureHpa = NAN;
  float humidityPct = NAN;
  float gasKOhms = NAN;
  float altitudeM = NAN;
};

class EnvService {
public:
  bool begin(TwoWire &wire = Wire);
  EnvReading readSample(float seaLevelPressureHpa = 1013.25f);
  bool isInitialized() const { return initialized_; }

private:
  bool initializeSensor();

  TwoWire *wire_ = nullptr;
  Adafruit_BME680 *sensor_ = nullptr;
  bool initialized_ = false;
  bool primed_ = false;
};

} // namespace raven
