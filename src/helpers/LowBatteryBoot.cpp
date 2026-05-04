#include "LowBatteryBoot.h"
#include "../HublinkNode.h"
#include <cmath>
#include <esp_sleep.h>

namespace raven {
namespace {

RTC_DATA_ATTR bool s_lowBatterySleepPending = false;

bool depleted(const BatteryReading &b, const LowBatteryGateConfig &cfg) {
  if (b.stateOfChargePct <= cfg.minSocTripPct) {
    return true;
  }
  if (!std::isnan(cfg.minCellVoltageTripV) && b.voltageV <= cfg.minCellVoltageTripV) {
    return true;
  }
  return false;
}

bool recovered(const BatteryReading &b, const LowBatteryGateConfig &cfg) {
  if (b.stateOfChargePct < cfg.minSocRecoverPct) {
    return false;
  }
  if (!std::isnan(cfg.minCellVoltageTripV)) {
    const float vRec = std::isnan(cfg.minCellVoltageRecoverV)
                           ? (cfg.minCellVoltageTripV + 0.05f)
                           : cfg.minCellVoltageRecoverV;
    if (b.voltageV < vRec) {
      return false;
    }
  }
  return true;
}

void enterLowBatteryDeepSleep(HublinkNode &node, const LowBatteryGateConfig &cfg) {
  node.setStatusLeds(false);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(cfg.lowBatteryRetrySleepSeconds) * 1000000ULL);
  esp_deep_sleep_start();
}

} // namespace

void runLowBatteryBootGate(HublinkNode &node, const BatteryReading &battery, bool usbPresent,
                           const LowBatteryGateConfig &cfg) {
  if (usbPresent) {
    s_lowBatterySleepPending = false;
    return;
  }

  const bool valid = battery.status == ServiceStatus::Ok && battery.hasCellReading;

  if (s_lowBatterySleepPending) {
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
      s_lowBatterySleepPending = false;
    } else {
      if (!valid) {
        if (Serial) {
          Serial.println(F("Raven: low-battery retry wake, gauge invalid; allowing boot"));
        }
        s_lowBatterySleepPending = false;
        return;
      }
      if (recovered(battery, cfg)) {
        s_lowBatterySleepPending = false;
        if (Serial) {
          Serial.println(F("Raven: battery recovered above resume threshold"));
        }
        return;
      }
      if (Serial) {
        Serial.print(F("Raven: battery still low; deep sleep "));
        Serial.print(cfg.lowBatteryRetrySleepSeconds);
        Serial.println(F("s"));
      }
      enterLowBatteryDeepSleep(node, cfg);
    }
  }

  if (!valid) {
    if (Serial) {
      Serial.println(F("Raven: battery gauge unavailable; boot continues"));
    }
    return;
  }

  if (depleted(battery, cfg)) {
    s_lowBatterySleepPending = true;
    if (Serial) {
      Serial.print(F("Raven: low battery (SOC="));
      Serial.print(battery.stateOfChargePct, 1);
      Serial.print(F("% V="));
      Serial.print(battery.voltageV, 3);
      Serial.print(F("V); deep sleep "));
      Serial.print(cfg.lowBatteryRetrySleepSeconds);
      Serial.println(F("s"));
    }
    enterLowBatteryDeepSleep(node, cfg);
  }
}

} // namespace raven
