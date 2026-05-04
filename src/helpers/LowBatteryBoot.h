#pragma once

#include "../services/PowerGaugeService.h"
#include <Arduino.h>

namespace raven {

class HublinkNode;

/// SOC-first low-battery boot gate (buck-boost aware). Optional cell-voltage legs use NAN to
/// disable. Recovery thresholds must be at or above trip thresholds to avoid oscillation.
struct LowBatteryGateConfig {
  float minSocTripPct = 5.0f;
  float minSocRecoverPct = 8.0f;
  float minCellVoltageTripV = NAN;
  float minCellVoltageRecoverV = NAN;
  uint32_t lowBatteryRetrySleepSeconds = 600;
};

/// USB skips the gate. Missing/invalid gauge allows boot (log when Serial is ready).
/// If the pack is depleted, enables timer wake and deep-sleeps (does not return).
void runLowBatteryBootGate(HublinkNode &node, const BatteryReading &battery, bool usbPresent,
                           const LowBatteryGateConfig &cfg = LowBatteryGateConfig{});

} // namespace raven
