#pragma once

#include <ArduinoJson.h>

namespace raven {

// Traverse a JSON object using dot-separated keys (same rules as MetaConfigEditor).
// Root must be a JSON object. Returns Null variant if missing or invalid path.
JsonVariantConst resolveMetaDotPath(JsonVariantConst root, const String &path,
                                  bool *ok = nullptr);

} // namespace raven
