#include "EnvService.h"

namespace raven {

bool EnvService::initializeSensor() {
  if (initialized_) {
    return true;
  }
  if (!sensor_) {
    sensor_ = new Adafruit_BME680(wire_);
  }

  bool ok = sensor_->begin(0x77, true);
  if (!ok) {
    ok = sensor_->begin(0x76, true);
  }
  if (!ok) {
    return false;
  }

  sensor_->setTemperatureOversampling(BME680_OS_8X);
  sensor_->setHumidityOversampling(BME680_OS_2X);
  sensor_->setPressureOversampling(BME680_OS_4X);
  sensor_->setIIRFilterSize(BME680_FILTER_SIZE_3);
  sensor_->setGasHeater(320, 150);
  // Prime one forced-mode cycle so first user-visible sample is less transient.
  sensor_->performReading();
  primed_ = true;
  initialized_ = true;
  return true;
}

bool EnvService::begin(TwoWire &wire) {
  wire_ = &wire;
  return initializeSensor();
}

EnvReading EnvService::readSample(float seaLevelPressureHpa) {
  EnvReading out;
  if (!wire_) {
    return out;
  }
  if (!initializeSensor()) {
    out.status = ServiceStatus::NotFound;
    return out;
  }
  if (!primed_) {
    sensor_->performReading();
    primed_ = true;
  }
  if (!sensor_->performReading()) {
    out.status = ServiceStatus::ReadFailed;
    return out;
  }

  const float pressureHpa = sensor_->pressure / 100.0f;
  out.status = ServiceStatus::Ok;
  out.temperatureC = sensor_->temperature;
  out.pressureHpa = pressureHpa;
  out.humidityPct = sensor_->humidity;
  out.gasKOhms = sensor_->gas_resistance / 1000.0f;
  out.altitudeM =
      44330.0f * (1.0f - powf(pressureHpa / seaLevelPressureHpa, 0.1903f));
  return out;
}

} // namespace raven
