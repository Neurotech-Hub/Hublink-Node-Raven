#pragma once

#include "../services/SdService.h"
#include <RTClib.h>
#include <WString.h>

namespace raven {

struct BeamFileNamingParams {
  const char *deviceId = "001";
  bool newFileOnBoot = true;
  bool isWakeFromSleep = false;
};

/// Resolve the active BEAM log path: `/BEAM{deviceId}_YYYYMMDDXX.csv` (2-digit XX).
/// Stores the chosen path in Preferences (`beamlog` / `filename`). Does not create the file.
String resolveBeamLogFilePath(SdService &sd, const BeamFileNamingParams &params,
                              const DateTime &now);

} // namespace raven
