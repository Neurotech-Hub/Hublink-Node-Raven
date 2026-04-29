#include "LogFileNaming.h"

namespace raven {
namespace {

String formatDatedPath(const char *baseName, const DateTime &now, bool includeHourMinute) {
  char path[40];
  if (includeHourMinute) {
    snprintf(path, sizeof(path), "/%s_%04d%02d%02d%02d%02d.csv", baseName, now.year(),
             now.month(), now.day(), now.hour(), now.minute());
  } else {
    snprintf(path, sizeof(path), "/%s_%04d%02d%02d.csv", baseName, now.year(), now.month(),
             now.day());
  }
  return String(path);
}

String formatManualPath(const char *baseName, uint32_t counter) {
  char path[32];
  snprintf(path, sizeof(path), "/%s_%05lu.csv", baseName, static_cast<unsigned long>(counter));
  return String(path);
}

} // namespace

bool isValidBaseName(const char *baseName) {
  if (!baseName || !baseName[0]) {
    return false;
  }
  for (size_t i = 0; baseName[i] != '\0'; ++i) {
    const char c = baseName[i];
    const bool isAlphaNum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9');
    const bool isAllowedSymbol = (c == '_') || (c == '-');
    if (!isAlphaNum && !isAllowedSymbol) {
      return false;
    }
  }
  return true;
}

String buildLogFilePath(const LogFilePolicy &policy, const DateTime &now) {
  if (!isValidBaseName(policy.baseName)) {
    return "";
  }

  switch (policy.mode) {
  case FileNameMode::Daily:
    return formatDatedPath(policy.baseName, now, false);
  case FileNameMode::Hourly:
    return formatDatedPath(policy.baseName, now, true);
  case FileNameMode::Manual:
    return formatManualPath(policy.baseName, policy.manualCounter);
  case FileNameMode::Disabled:
    return String("/") + policy.baseName + ".csv";
  }
  return "";
}

ServiceStatus resolveLogFilePath(SdService &sd, LogFilePolicy &policy,
                                 const RtcReading &clockReading, String &outPath) {
  outPath = "";
  if (!isValidBaseName(policy.baseName)) {
    return ServiceStatus::InvalidData;
  }

  if (policy.mode == FileNameMode::Manual && !policy.manualCounterInitialized) {
    const ServiceStatus initStatus = initializeManualCounter(sd, policy);
    if (initStatus != ServiceStatus::Ok) {
      return initStatus;
    }
  }

  const DateTime now = (clockReading.status == ServiceStatus::Ok && clockReading.now.isValid())
                           ? clockReading.now
                           : fallbackDateTime();
  outPath = buildLogFilePath(policy, now);
  return outPath.length() > 0 ? ServiceStatus::Ok : ServiceStatus::InvalidData;
}

DateTime fallbackDateTime() { return DateTime(F(__DATE__), F(__TIME__)); }

ServiceStatus initializeManualCounter(SdService &sd, LogFilePolicy &policy) {
  if (!isValidBaseName(policy.baseName)) {
    return ServiceStatus::InvalidData;
  }
  if (policy.mode != FileNameMode::Manual) {
    policy.manualCounterInitialized = false;
    return ServiceStatus::InvalidData;
  }

  for (uint32_t probe = 0; probe <= 99999; ++probe) {
    const String path = formatManualPath(policy.baseName, probe);
    if (!sd.exists(path.c_str())) {
      policy.manualCounter = probe;
      policy.manualCounterInitialized = true;
      return ServiceStatus::Ok;
    }
  }

  return ServiceStatus::InvalidData;
}

void incrementManualCounter(LogFilePolicy &policy) {
  if (policy.mode != FileNameMode::Manual) {
    return;
  }
  if (policy.manualCounter < 99999) {
    ++policy.manualCounter;
  }
}

} // namespace raven
