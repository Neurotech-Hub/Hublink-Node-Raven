#pragma once

#include "hardware/RavenPins.h"
#include "services/EnvService.h"
#include "services/LightService.h"
#include "services/MagnetCounterService.h"
#include "services/MotionCounterService.h"
#include "services/PowerGaugeService.h"
#include "services/RtcService.h"
#include "services/SdService.h"
#include <esp_sleep.h>
#include <Wire.h>

namespace raven {

class HublinkNode {
public:
  /// Default CPU clock before pin setup. 80 MHz is a common choice for reliable Wi‑Fi / Bluetooth on ESP32-S3.
  static constexpr uint32_t kDefaultMcuClockMhz = 80;

  /// Set the ESP32 CPU frequency (Arduino-ESP32 `setCpuFrequencyMhz`). Typical values: 80, 160, 240 MHz.
  static bool setMcuClockMhz(uint32_t mhz);
  /// Current CPU frequency in MHz (from `getCpuFrequencyMhz()`).
  static uint32_t mcuClockMhz();

  /// @param mcuClockMhz Applied first via `setMcuClockMhz`; default is `kDefaultMcuClockMhz`.
  bool beginHardware(uint32_t mcuClockMhz = kDefaultMcuClockMhz);
  bool beginI2C(uint32_t clockHz = DEFAULT_I2C_CLOCK_HZ);

  void setI2CPowerEnabled(bool enabled);
  bool isI2CPowerEnabled() const;
  /// Drive the SD power gate (`PIN_SD_EN`, active-low) directly without managing the FS state.
  /// Most sketches should prefer `sd().begin()` / `sd().end()` (which also handle SPI and FS),
  /// or the combined `setExternalRailsEnabled` below for deep-sleep prep.
  void setSdPowerEnabled(bool enabled);
  bool isSdPowerEnabled() const;
  /// Enable or disable both the I2C aux rail and the SD rail with one call. The disable path
  /// closes the SD filesystem (`sd().end()`) before dropping its rail, then drops the I2C rail.
  /// The enable path drives the I2C rail on first, then mounts SD (`sd().begin()`). Useful as
  /// `setExternalRailsEnabled(false)` immediately before `esp_deep_sleep_start()` so neither gate
  /// continues to draw current during sleep.
  void setExternalRailsEnabled(bool enabled);

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
  MotionCounterService &motionCounter() { return motionCounter_; }

private:
  bool hardwareInitialized_ = false;

  SdService sd_;
  RtcService rtc_;
  PowerGaugeService powerGauge_;
  LightService light_;
  EnvService environment_;
  MagnetCounterService magnetCounter_;
  MotionCounterService motionCounter_;
};

} // namespace raven
