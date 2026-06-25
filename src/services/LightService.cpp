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

  // Max sensitivity: gain 2x, 800 ms integration (VEML7700 limits).
  sensor_.setGain(VEML7700_GAIN_2);
  sensor_.setIntegrationTime(VEML7700_IT_800MS);
  sensor_.interruptEnable(false);
  sensor_.powerSaveEnable(false);
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
  // After shutdown, readWait() can skip delaying because lastRead is stale. Wait a
  // full integration window explicitly (matches Adafruit readWait × 2).
  const uint32_t integrationWaitMs =
      static_cast<uint32_t>(sensor_.getIntegrationTimeValue()) * 2U;
  delay(integrationWaitMs);
  // Single ALS sample; corrected lux for low-light nonlinearity.
  out.lux = sensor_.readLux(VEML_LUX_CORRECTED_NOWAIT);
  out.als = sensor_.readALS(false);
  out.white = sensor_.readWhite(false);
  const uint16_t irq = sensor_.interruptStatus();
  sensor_.enable(false);

  out.lowThreshold = (irq & VEML7700_INTERRUPT_LOW) != 0;
  out.highThreshold = (irq & VEML7700_INTERRUPT_HIGH) != 0;
  out.status = ServiceStatus::Ok;
  return out;
}

} // namespace raven
