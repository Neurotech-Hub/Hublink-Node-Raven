#include "RtcService.h"
#include <sys/time.h>
#include <time.h>

namespace raven {

void RtcService::syncSystemTimeFromRtc(const DateTime &now) {
  if (!now.isValid()) {
    return;
  }
  const time_t epoch = static_cast<time_t>(now.unixtime());
  if (epoch < 1700000000 || epoch > 2200000000) {
    return;
  }
  struct timeval tv = {};
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

bool RtcService::begin(TwoWire &wire, bool setOnLostPower) {
  wire_ = &wire;
  setOnLostPower_ = setOnLostPower;

  if (!initialized_) {
    if (!rtc_.begin(wire_)) {
      return false;
    }
    if (setOnLostPower_ && rtc_.lostPower()) {
      rtc_.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    initialized_ = true;
  }

  // Best-effort: keep ESP system clock aligned to RTC when RTC time is valid.
  syncSystemTimeFromRtc(rtc_.now());
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
  syncSystemTimeFromRtc(out.now);
  return out;
}

bool RtcService::adjust(const DateTime &dt) {
  if (!initialized_) {
    return false;
  }
  rtc_.adjust(dt);
  syncSystemTimeFromRtc(dt);
  return true;
}

} // namespace raven
