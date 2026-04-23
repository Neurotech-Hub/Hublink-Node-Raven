#include "RtcService.h"

namespace raven {

bool RtcService::begin(TwoWire &wire, bool setOnLostPower) {
  wire_ = &wire;
  setOnLostPower_ = setOnLostPower;

  if (initialized_) {
    return true;
  }
  if (!rtc_.begin(wire_)) {
    return false;
  }
  if (setOnLostPower_ && rtc_.lostPower()) {
    rtc_.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  initialized_ = true;
  return true;
}

RtcReading RtcService::readSample() {
  RtcReading out;
  if (!wire_) {
    return out;
  }
  if (!initialized_ && !begin(*wire_, setOnLostPower_)) {
    out.status = ServiceStatus::NotFound;
    return out;
  }

  out.hadLostPower = rtc_.lostPower();
  out.now = rtc_.now();
  if (!out.now.isValid()) {
    out.status = ServiceStatus::InvalidData;
    return out;
  }

  out.temperatureC = rtc_.getTemperature();
  out.status = ServiceStatus::Ok;
  return out;
}

bool RtcService::adjust(const DateTime &dt) {
  if (!initialized_) {
    return false;
  }
  rtc_.adjust(dt);
  return true;
}

} // namespace raven
