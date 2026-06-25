#include "BeamCsvLogger.h"
#include <cstdio>

namespace raven {

String BeamCsvLogger::header() {
  return String(F("datetime,millis,device_id,library_version,battery_voltage,temperature_c,"
                  "pressure_hpa,humidity_percent,lux,activity_count,activity_percent,"
                  "inactivity_period_s,inactivity_count,inactivity_percent,min_free_heap,reboot"));
}

bool BeamCsvLogger::ensureLogFile(SdService &sd, const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (sd.exists(path)) {
    return true;
  }
  return sd.appendLine(path, header()) == ServiceStatus::Ok;
}

String BeamCsvLogger::formatRow(const BeamLogSample &s) {
  char row[192];
  snprintf(row, sizeof(row),
           "%04d-%02d-%02d %02d:%02d:%02d,%lu,%s,%s,%.3f,%.2f,%.2f,%.2f,%.4f,%u,%.3f,%u,%u,%.3f,"
           "%lu,%u",
           s.year, s.month, s.day, s.hour, s.minute, s.second,
           static_cast<unsigned long>(s.millisStamp),
           s.deviceId != nullptr ? s.deviceId : "001",
           s.libraryVersion != nullptr ? s.libraryVersion : "0.0.0",
           static_cast<double>(s.batteryVoltage), static_cast<double>(s.temperatureC),
           static_cast<double>(s.pressureHpa), static_cast<double>(s.humidityPct),
           static_cast<double>(s.lux), static_cast<unsigned>(s.activityCount), s.activityPercent,
           static_cast<unsigned>(s.inactivityPeriodS),
           static_cast<unsigned>(s.inactivityCount), s.inactivityPercent,
           static_cast<unsigned long>(s.minFreeHeap), static_cast<unsigned>(s.reboot));
  return String(row);
}

} // namespace raven
