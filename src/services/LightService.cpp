#include "LightService.h"

namespace raven {

bool LightService::begin(TwoWire &wire) {
  wire_ = &wire;
  if (initialized_) {
    return true;
  }
  if (!sensor_.begin(wire_)) {
    return false;
  }

  // Default to high-sensitivity ALS settings for low-light deployments.
  sensor_.setGain(VEML7700_GAIN_2);
  sensor_.setIntegrationTime(VEML7700_IT_800MS);
  sensor_.setLowThreshold(10000);
  sensor_.setHighThreshold(20000);
  sensor_.interruptEnable(true);
  sensor_.powerSaveEnable(true);
  sensor_.enable(false);

  initialized_ = true;
  return true;
}

LightReading LightService::readSample() {
  LightReading out;
  if (!wire_) {
    return out;
  }
  if (!initialized_ && !begin(*wire_)) {
    out.status = ServiceStatus::NotFound;
    return out;
  }

  sensor_.enable(true);
  // One-shot mode needs an integration window before lux can settle.
  // Match the default low-light 800 ms integration with margin.
  delay(850);
  out.als = sensor_.readALS(true);
  out.white = sensor_.readWhite(false);
  out.lux = sensor_.readLux(VEML_LUX_NORMAL_NOWAIT);
  const uint16_t irq = sensor_.interruptStatus();
  sensor_.enable(false);

  out.lowThreshold = (irq & VEML7700_INTERRUPT_LOW) != 0;
  out.highThreshold = (irq & VEML7700_INTERRUPT_HIGH) != 0;
  out.status = ServiceStatus::Ok;
  return out;
}

} // namespace raven
