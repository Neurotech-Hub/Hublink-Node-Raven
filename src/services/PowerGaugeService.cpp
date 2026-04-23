#include "PowerGaugeService.h"

namespace raven {

bool PowerGaugeService::begin(TwoWire &wire) {
  wire_ = &wire;
  if (initialized_) {
    return true;
  }
  if (!sensor_.begin(wire_)) {
    return false;
  }
  delay(200);
  chipId_ = sensor_.getChipID();
  sensor_.hibernate();
  initialized_ = true;
  return true;
}

BatteryReading PowerGaugeService::readSample() {
  BatteryReading out;
  if (!wire_) {
    return out;
  }
  if (!initialized_ && !begin(*wire_)) {
    out.status = ServiceStatus::NotFound;
    return out;
  }

  sensor_.wake();
  delay(3);
  const float voltage = sensor_.cellVoltage();
  const float soc = sensor_.cellPercent();
  sensor_.hibernate();

  if (isnan(voltage) || isnan(soc)) {
    out.status = ServiceStatus::ReadFailed;
    return out;
  }

  // Guard against floating VCELL pins (common when no battery is attached).
  // A "valid" voltage with 0% SOC is usually not a real pack reading.
  if (soc <= 0.0f || soc > 100.0f || voltage < 2.0f || voltage > 5.5f) {
    out.status = ServiceStatus::InvalidData;
    return out;
  }

  out.status = ServiceStatus::Ok;
  out.voltageV = voltage;
  out.stateOfChargePct = soc;
  out.hasCellReading = true;
  return out;
}

} // namespace raven
