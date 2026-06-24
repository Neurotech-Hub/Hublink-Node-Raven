#pragma once

#include "../hardware/RavenPins.h"
#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_err.h>
#include <esp_sleep.h>
#include <soc/rtc_io_reg.h>
#include <ulp_common.h>
#include <esp32s3/ulp.h>

namespace raven {

/// BEAM-style ULP motion counter: +1 per 1-second sampling window where the sensor pin is active.
/// Default pin is PIN_AUX_GPIO1 with internal pulldown (HIGH = motion).
class MotionCounterService {
public:
  bool begin(gpio_num_t sensorPin = static_cast<gpio_num_t>(PIN_AUX_GPIO1));
  bool start();
  uint16_t motionCount() const;
  void clearCount();

private:
  bool initialized_ = false;
  gpio_num_t sensorPin_ = static_cast<gpio_num_t>(PIN_AUX_GPIO1);
  uint8_t rtcGpioIndex_ = 0;
};

} // namespace raven
