#pragma once

#include "../services/SdService.h"
#include <ArduinoJson.h>

namespace raven {

// Load and parse meta JSON from SD. Root must be an object.
bool loadMetaJson(SdService &sd, JsonDocument &outDoc, const char *path = "/meta.json",
                  Print *errOut = nullptr);

// Typed reads at dot-separated paths ("wheel.sleep_time_seconds"). Missing or wrong-type
// values return false; out is unchanged.

bool metaGetLong(const JsonDocument &doc, const String &dotPath, long &out);

bool metaGetUInt32(const JsonDocument &doc, const String &dotPath, uint32_t &out);

bool metaGetBool(const JsonDocument &doc, const String &dotPath, bool &out);

bool metaGetString(const JsonDocument &doc, const String &dotPath, String &out);

bool metaGetJsonArray(const JsonDocument &doc, const String &dotPath, JsonArrayConst &out);

} // namespace raven
