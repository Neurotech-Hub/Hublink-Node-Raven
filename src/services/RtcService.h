#pragma once

#include "RTClib.h"
#include "ServiceTypes.h"
#include <Wire.h>

namespace raven {

struct RtcReading {
  ServiceStatus status = ServiceStatus::NotInitialized;
  DateTime now;
  float temperatureC = NAN;
  bool hadLostPower = false;
};

class RtcService {
public:
  bool begin(TwoWire &wire = Wire, bool setOnLostPower = true);
  RtcReading readSample();
  bool adjust(const DateTime &dt);
  bool isInitialized() const { return initialized_; }

private:
  TwoWire *wire_ = nullptr;
  RTC_DS3231 rtc_;
  bool initialized_ = false;
  bool setOnLostPower_ = true;
};

} // namespace raven
