#pragma once

#include "../services/PowerGaugeService.h"
#include <Arduino.h>

namespace raven {

class HublinkNode;

/// Default trip / recover cell voltages (hardware policy). Override only in advanced paths.
inline constexpr float kSafeguardTripVoltsDefault = 2.0f;
inline constexpr float kSafeguardRecoverVoltsDefault = 2.6f;
/// How often the automatic path may sample while awake (`millis()`), and default timer-wake retry interval.
inline constexpr uint32_t kSafeguardPollIntervalSecondsDefault = 600;
inline constexpr uint32_t kSafeguardShutdownWakeupSecondsDefault = 600;

/// Backend policy for `DataLoggerHelper::begin()` and advanced tuning.
/// Sketches normally use the bool overload of `maybeAutomaticVoltageSafeguard`, `isCellBelowTripVoltage`,
/// and `safeguardShutdown` instead.
struct LowBatteryGateConfig {
  float minCellVoltageTripV = kSafeguardTripVoltsDefault;
  float minCellVoltageRecoverV = kSafeguardRecoverVoltsDefault;
  uint32_t safeguardIntervalSeconds = kSafeguardPollIntervalSecondsDefault;
  bool enableAutomaticSafeguard = true;
  uint32_t lowBatteryRetrySleepSeconds = kSafeguardShutdownWakeupSecondsDefault;
};

/// **Simple automatic path:** when `enabled`, uses internal defaults (same as data-logger). Uses
/// **`millis()`** only in RAM; first call after each reset always runs. **`beginI2C()`** does not
/// invoke this—call from your sketch or rely on `DataLoggerHelper::begin()`.
void maybeAutomaticVoltageSafeguard(HublinkNode &node, bool enabled = true);

/// **Advanced:** full policy (interval, trip V, sleep duration, etc.).
void maybeAutomaticVoltageSafeguard(HublinkNode &node, const LowBatteryGateConfig &cfg);

/// True if the last cell sample is valid and **cell V ≤ tripVolts** (default: `kSafeguardTripVoltsDefault`).
/// Initializes the gauge if needed. Does **not** check USB—you decide what to do next.
bool isCellBelowTripVoltage(HublinkNode &node, float tripVolts = kSafeguardTripVoltsDefault);

/// Turn off status LEDs and enter **timer deep sleep** for `wakeupInSeconds` (does not return).
/// Caller should gate on USB, logging flush, etc., if required.
void safeguardShutdown(HublinkNode &node, uint32_t wakeupInSeconds);

/// One sample to `Stream`; never sleeps. Uses default trip/recover in output unless `cfg` is passed.
bool diagnoseVoltageSafeguard(Stream &io, HublinkNode &node, bool usbPresent,
                              const LowBatteryGateConfig &cfg = LowBatteryGateConfig{});

} // namespace raven
