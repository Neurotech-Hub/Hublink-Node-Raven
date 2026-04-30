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
  snprintf(path, sizeof(path), "/%s_%03lu.csv", baseName, static_cast<unsigned long>(counter));
  return String(path);
}

String formatDatedIncrementedPath(const char *baseName, const DateTime &now,
                                  bool includeHourMinute, uint16_t counter) {
  char path[44];
  if (includeHourMinute) {
    snprintf(path, sizeof(path), "/%s_%04d%02d%02d%02d%02d%03u.csv", baseName, now.year(),
             now.month(), now.day(), now.hour(), now.minute(),
             static_cast<unsigned int>(counter));
  } else {
    snprintf(path, sizeof(path), "/%s_%04d%02d%02d%03u.csv", baseName, now.year(), now.month(),
             now.day(), static_cast<unsigned int>(counter));
  }
  return String(path);
}

String formatDisabledIncrementedPath(const char *baseName, uint16_t counter) {
  char path[32];
  snprintf(path, sizeof(path), "/%s%03u.csv", baseName, static_cast<unsigned int>(counter));
  return String(path);
}

String buildRebootCandidatePath(const LogFilePolicy &policy, const DateTime &now,
                                uint16_t counter) {
  switch (policy.mode) {
  case FileNameMode::Daily:
    return formatDatedIncrementedPath(policy.baseName, now, false, counter);
  case FileNameMode::Hourly:
    return formatDatedIncrementedPath(policy.baseName, now, true, counter);
  case FileNameMode::Manual:
    return formatManualPath(policy.baseName, counter);
  case FileNameMode::Disabled:
    return formatDisabledIncrementedPath(policy.baseName, counter);
  }
  return "";
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

  if (policy.incOnReboot) {
    return buildRebootCandidatePath(policy, now, policy.mode == FileNameMode::Manual
                                                  ? static_cast<uint16_t>(policy.manualCounter)
                                                  : policy.rebootCounter);
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

  const DateTime now = (clockReading.status == ServiceStatus::Ok && clockReading.now.isValid())
                           ? clockReading.now
                           : fallbackDateTime();
  if (policy.mode == FileNameMode::Manual && !policy.manualCounterInitialized) {
    const ServiceStatus initStatus = initializeManualCounter(sd, policy);
    if (initStatus != ServiceStatus::Ok) {
      return initStatus;
    }
  } else if (policy.mode != FileNameMode::Manual && policy.incOnReboot &&
             !policy.rebootCounterInitialized) {
    for (uint16_t probe = 0; probe <= 999; ++probe) {
      const String path = buildRebootCandidatePath(policy, now, probe);
      if (!sd.exists(path.c_str())) {
        policy.rebootCounter = probe;
        policy.rebootCounterInitialized = true;
        break;
      }
    }
    if (!policy.rebootCounterInitialized) {
      return ServiceStatus::InvalidData;
    }
  }

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

  if (policy.incOnReboot) {
    for (uint16_t probe = 0; probe <= 999; ++probe) {
      const String path = formatManualPath(policy.baseName, probe);
      if (!sd.exists(path.c_str())) {
        policy.manualCounter = probe;
        policy.manualCounterInitialized = true;
        return ServiceStatus::Ok;
      }
    }
    return ServiceStatus::InvalidData;
  }

  for (int probe = 999; probe >= 0; --probe) {
    const String path = formatManualPath(policy.baseName, static_cast<uint32_t>(probe));
    if (sd.exists(path.c_str())) {
      policy.manualCounter = static_cast<uint32_t>(probe);
      policy.manualCounterInitialized = true;
      return ServiceStatus::Ok;
    }
  }

  policy.manualCounter = 0;
  policy.manualCounterInitialized = true;
  return ServiceStatus::Ok;
}

void incrementManualCounter(LogFilePolicy &policy) {
  if (policy.mode != FileNameMode::Manual) {
    return;
  }
  if (policy.manualCounter < 999) {
    ++policy.manualCounter;
  }
  policy.manualCounterInitialized = true;
}

} // namespace raven
