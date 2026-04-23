#pragma once

#include "../hardware/HublinkPins.h"
#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_err.h>
#include <soc/rtc_io_reg.h>
#include <ulp_common.h>
#include <esp32s3/ulp.h>

namespace raven {

class MagnetCounterService {
public:
  bool begin(gpio_num_t sensorPin = static_cast<gpio_num_t>(PIN_AUX_GPIO0));
  bool start();
  uint16_t edgeCount() const;
  uint16_t magnetPassCount() const;
  void clearCount();

private:
  bool initialized_ = false;
  gpio_num_t sensorPin_ = static_cast<gpio_num_t>(PIN_AUX_GPIO0);
  uint8_t rtcGpioIndex_ = 0;
};

} // namespace raven
