#pragma once

#include "../services/SdService.h"
#include <WString.h>

namespace raven {

/// One BEAM CSV row (legacy Hublink-BEAM Data Logging section).
struct BeamLogSample {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  uint32_t millisStamp = 0;
  const char *deviceId = "001";
  const char *libraryVersion = "0.2.1";
  float batteryVoltage = -1.0f;
  float temperatureC = -273.15f;
  float pressureHpa = -1.0f;
  float humidityPct = -1.0f;
  float lux = -1.0f;
  uint16_t activityCount = 0;
  double activityPercent = 0.0;
  uint16_t inactivityPeriodS = 0;
  uint16_t inactivityCount = 0;
  double inactivityPercent = 0.0;
};

class BeamCsvLogger {
public:
  static String header();
  static String formatRow(const BeamLogSample &sample);
  /// Create the CSV with header row when path is missing; no-op when it already exists.
  static bool ensureLogFile(SdService &sd, const char *path);
};

} // namespace raven
