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
  // Adafruit::begin() leaves enableSleep(false). Allow CONFIG sleep (~1 µA) between reads; I2C
  // transactions use sleep(false) first. Hibernate reduces internal gauge activity when idle.
  sensor_.enableSleep(true);
  sensor_.hibernate();
  sensor_.sleep(true);
  initialized_ = true;
  return true;
}

void PowerGaugeService::setAlertVoltages(float minCellV, float maxCellV) {
  if (!initialized_) {
    return;
  }
  sensor_.setAlertVoltages(minCellV, maxCellV);
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

  sensor_.sleep(false);
  sensor_.wake();
  delay(3);
  const float voltage = sensor_.cellVoltage();
  const float soc = sensor_.cellPercent();
  sensor_.hibernate();
  sensor_.sleep(true);

  if (isnan(voltage)) {
    out.status = ServiceStatus::ReadFailed;
    return out;
  }

  // Guard against floating VCELL pins (common when no battery is attached).
  if (voltage < 2.0f || voltage > 5.5f) {
    out.status = ServiceStatus::InvalidData;
    return out;
  }

  out.status = ServiceStatus::Ok;
  out.voltageV = voltage;
  if (!isnan(soc) && soc >= 0.0f) {
    out.stateOfChargePct = min(soc, 100.0f);
  }
  out.hasCellReading = true;
  return out;
}

} // namespace raven
