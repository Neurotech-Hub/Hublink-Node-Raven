#pragma once

#include "../services/RtcService.h"
#include "../services/SdService.h"

namespace raven {

enum class FileNameMode : uint8_t {
  Daily = 0,
  Hourly,
  Manual,
  Disabled,
};

struct LogFilePolicy {
  const char *baseName = nullptr;
  FileNameMode mode = FileNameMode::Daily;
  uint32_t manualCounter = 0;
  bool manualCounterInitialized = false;
};

bool isValidBaseName(const char *baseName);
String buildLogFilePath(const LogFilePolicy &policy, const DateTime &now);
ServiceStatus resolveLogFilePath(SdService &sd, LogFilePolicy &policy,
                                 const RtcReading &clockReading, String &outPath);
DateTime fallbackDateTime();
ServiceStatus initializeManualCounter(SdService &sd, LogFilePolicy &policy);
void incrementManualCounter(LogFilePolicy &policy);

} // namespace raven
