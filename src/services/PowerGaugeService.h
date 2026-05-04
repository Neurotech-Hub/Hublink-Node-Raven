#pragma once

#include "ServiceTypes.h"
#include <Adafruit_MAX1704X.h>
#include <Wire.h>

namespace raven {

struct BatteryReading {
  ServiceStatus status = ServiceStatus::NotInitialized;
  float voltageV = NAN;
  float stateOfChargePct = NAN;
  bool hasCellReading = false;
};

class PowerGaugeService {
public:
  bool begin(TwoWire &wire = Wire);
  BatteryReading readSample();
  uint8_t chipId() const { return chipId_; }
  bool isInitialized() const { return initialized_; }

  /// Optional alert window (volts). No-op if the gauge did not initialize.
  void setAlertVoltages(float minCellV, float maxCellV);

private:
  TwoWire *wire_ = nullptr;
  Adafruit_MAX17048 sensor_;
  bool initialized_ = false;
  uint8_t chipId_ = 0;
};

} // namespace raven
