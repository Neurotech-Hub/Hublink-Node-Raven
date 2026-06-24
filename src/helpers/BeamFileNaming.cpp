#include "BeamFileNaming.h"
#include <Preferences.h>
#include <cstdio>

namespace raven {
namespace {

constexpr char kPrefsNamespace[] = "beamlog";
constexpr char kPrefsFilenameKey[] = "filename";

bool parseStoredDate(const String &storedFilename, int &year, int &month, int &day) {
  const int underscorePos = storedFilename.indexOf('_');
  if (underscorePos < 0) {
    return false;
  }
  const int dateStart = underscorePos + 1;
  if (storedFilename.length() < static_cast<unsigned>(dateStart + 8)) {
    return false;
  }
  year = storedFilename.substring(dateStart, dateStart + 4).toInt();
  month = storedFilename.substring(dateStart + 4, dateStart + 6).toInt();
  day = storedFilename.substring(dateStart + 6, dateStart + 8).toInt();
  return year > 0 && month > 0 && day > 0;
}

bool isSameCalendarDay(const DateTime &now, int year, int month, int day) {
  return now.year() == year && now.month() == month && now.day() == day;
}

void storeFilename(const String &path) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putString(kPrefsFilenameKey, path);
  prefs.end();
}

String buildBeamPath(const char *deviceId, const char *dateCompact, uint8_t seq) {
  char path[32];
  snprintf(path, sizeof(path), "/BEAM%s_%s%02u.csv", deviceId, dateCompact,
           static_cast<unsigned>(seq));
  return String(path);
}

String readStoredFilename() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  const String stored = prefs.getString(kPrefsFilenameKey, "");
  prefs.end();
  return stored;
}

} // namespace

String resolveBeamLogFilePath(SdService &sd, const BeamFileNamingParams &params,
                              const DateTime &now) {
  char dateCompact[12];
  snprintf(dateCompact, sizeof(dateCompact), "%04d%02d%02d", now.year(), now.month(),
           now.day());

  const char *deviceId =
      (params.deviceId != nullptr && params.deviceId[0] != '\0') ? params.deviceId : "001";

  const String storedFilename = readStoredFilename();
  if (storedFilename.length() > 0) {
    int storedYear = 0;
    int storedMonth = 0;
    int storedDay = 0;
    if (parseStoredDate(storedFilename, storedYear, storedMonth, storedDay) &&
        isSameCalendarDay(now, storedYear, storedMonth, storedDay) &&
        sd.exists(storedFilename.c_str())) {
      if (params.isWakeFromSleep) {
        return storedFilename;
      }
      if (!params.newFileOnBoot) {
        return storedFilename;
      }
    }
  }

  if (!params.newFileOnBoot) {
    for (uint8_t num = 0; num < 100; ++num) {
      const String candidate = buildBeamPath(deviceId, dateCompact, num);
      if (sd.exists(candidate.c_str())) {
        storeFilename(candidate);
        return candidate;
      }
    }
  }

  for (uint8_t num = 0; num < 100; ++num) {
    const String candidate = buildBeamPath(deviceId, dateCompact, num);
    if (!sd.exists(candidate.c_str())) {
      storeFilename(candidate);
      return candidate;
    }
  }

  return String();
}

} // namespace raven
