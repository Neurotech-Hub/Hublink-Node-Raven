#pragma once

#include "hardware/HublinkPins.h"
#include "services/EnvService.h"
#include "services/LightService.h"
#include "services/MagnetCounterService.h"
#include "services/PowerGaugeService.h"
#include "services/RtcService.h"
#include "services/SdService.h"
#include <esp_sleep.h>
#include <Wire.h>

namespace raven {

class HublinkNode {
public:
  bool beginHardware();
  bool beginI2C(uint32_t clockHz = DEFAULT_I2C_CLOCK_HZ);

  void setI2CPowerEnabled(bool enabled);
  bool isI2CPowerEnabled() const;

  bool readMagnet() const;
  bool readUsbSense() const;
  void setStatusLeds(bool on);
  esp_sleep_wakeup_cause_t wakeupCause() const;
  bool isTimerWake() const;

  SdService &sd() { return sd_; }
  RtcService &rtc() { return rtc_; }
  PowerGaugeService &powerGauge() { return powerGauge_; }
  LightService &light() { return light_; }
  EnvService &environment() { return environment_; }
  MagnetCounterService &magnetCounter() { return magnetCounter_; }

private:
  bool hardwareInitialized_ = false;

  SdService sd_;
  RtcService rtc_;
  PowerGaugeService powerGauge_;
  LightService light_;
  EnvService environment_;
  MagnetCounterService magnetCounter_;
};

} // namespace raven
