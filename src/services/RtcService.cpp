#include "RtcService.h"
#include <sys/time.h>
#include <time.h>

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

  // Best-effort: keep ESP system clock aligned to RTC when RTC time is valid.
  const DateTime now = rtc_.now();
  if (now.isValid()) {
    const time_t epoch = static_cast<time_t>(now.unixtime());
    if (epoch >= 1700000000 && epoch <= 2200000000) {
      struct timeval tv = {};
      tv.tv_sec = epoch;
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
    }
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
