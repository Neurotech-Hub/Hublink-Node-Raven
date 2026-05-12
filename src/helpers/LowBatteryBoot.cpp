#include "LowBatteryBoot.h"
#include "../HublinkNode.h"
#include <cmath>
#include <esp_sleep.h>

namespace raven {
namespace {

static uint32_t s_lastAutomaticCheckMs = 0;

bool cellAtOrBelowTrip(const BatteryReading &b, float tripVolts) {
  return !std::isnan(tripVolts) && b.voltageV <= tripVolts;
}

bool depletedVoltage(const BatteryReading &b, const LowBatteryGateConfig &cfg) {
  return cellAtOrBelowTrip(b, cfg.minCellVoltageTripV);
}

} // namespace

void safeguardShutdown(HublinkNode &node, uint32_t wakeupInSeconds) {
  node.setStatusLeds(false);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(wakeupInSeconds) * 1000000ULL);
  esp_deep_sleep_start();
}

bool isCellBelowTripVoltage(HublinkNode &node, float tripVolts) {
  (void)node.powerGauge().begin();
  const BatteryReading b = node.powerGauge().readSample();
  const bool valid = b.status == ServiceStatus::Ok && b.hasCellReading;
  if (!valid) {
    return false;
  }
  return cellAtOrBelowTrip(b, tripVolts);
}

void maybeAutomaticVoltageSafeguard(HublinkNode &node, bool enabled) {
  if (!enabled) {
    return;
  }
  maybeAutomaticVoltageSafeguard(node, LowBatteryGateConfig{});
}

void maybeAutomaticVoltageSafeguard(HublinkNode &node, const LowBatteryGateConfig &cfg) {
  if (!cfg.enableAutomaticSafeguard) {
    return;
  }
  const uint32_t now = millis();
  const uint32_t minGapMs =
      cfg.safeguardIntervalSeconds > 0 ? cfg.safeguardIntervalSeconds * 1000UL : 0UL;
  if (s_lastAutomaticCheckMs != 0 && minGapMs != 0U &&
      static_cast<uint32_t>(now - s_lastAutomaticCheckMs) < minGapMs) {
    return;
  }

  (void)node.powerGauge().begin();
  const bool usbPresent = node.readUsbSense();
  const BatteryReading b = node.powerGauge().readSample();
  const bool valid = b.status == ServiceStatus::Ok && b.hasCellReading;

  s_lastAutomaticCheckMs = millis();

  if (!valid) {
    return;
  }
  if (!depletedVoltage(b, cfg)) {
    return;
  }
  if (usbPresent) {
    if (Serial) {
      Serial.print(F("Raven: automatic safeguard: cell voltage low (V="));
      Serial.print(b.voltageV, 3);
      Serial.println(F(") USB present — not sleeping"));
    }
    return;
  }
  if (Serial) {
    Serial.print(F("Raven: automatic safeguard: cell voltage low (V="));
    Serial.print(b.voltageV, 3);
    Serial.print(F(") deep sleep "));
    Serial.print(cfg.lowBatteryRetrySleepSeconds);
    Serial.println(F("s"));
  }
  safeguardShutdown(node, cfg.lowBatteryRetrySleepSeconds);
}

bool diagnoseVoltageSafeguard(Stream &io, HublinkNode &node, bool usbPresent,
                              const LowBatteryGateConfig &cfg) {
  (void)node.powerGauge().begin();
  const BatteryReading b = node.powerGauge().readSample();
  io.println(F("--- safeguard diagnose (voltage only) ---"));
  io.print(F("usbPresent="));
  io.println(usbPresent ? F("true") : F("false"));
  io.print(F("autoEnabled="));
  io.println(cfg.enableAutomaticSafeguard ? F("true") : F("false"));
  io.print(F("intervalSeconds="));
  io.println(cfg.safeguardIntervalSeconds);
  io.print(F("status="));
  io.println(statusToString(b.status));
  io.print(F("hasCellReading="));
  io.println(b.hasCellReading ? F("true") : F("false"));
  io.print(F("V="));
  io.print(b.voltageV, 3);
  io.print(F("  tripV<="));
  io.print(cfg.minCellVoltageTripV, 2);
  io.print(F("  recoverV>="));
  io.print(cfg.minCellVoltageRecoverV, 2);
  io.println();
  io.print(F("SOC (informational)="));
  if (!std::isnan(b.stateOfChargePct)) {
    io.print(b.stateOfChargePct, 1);
    io.println(F("%"));
  } else {
    io.println(F("n/a"));
  }
  const bool valid = b.status == ServiceStatus::Ok && b.hasCellReading;
  const bool trip = valid && depletedVoltage(b, cfg);
  io.print(F("voltageTripWouldFire="));
  io.println(trip ? F("true") : F("false"));
  io.println(F("--- end diagnose ---"));
  return trip;
}

} // namespace raven
