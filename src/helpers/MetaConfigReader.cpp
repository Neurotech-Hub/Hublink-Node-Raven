#include "MetaConfigReader.h"
#include "MetaJsonPath.h"
#include <limits.h>
#include <stdint.h>

namespace raven {

namespace {

bool roughlyIntegralDouble(double value, long &outIntegral) {
  if (value < static_cast<double>(LONG_MIN) || value > static_cast<double>(LONG_MAX)) {
    return false;
  }
  const long rounded = static_cast<long>(value);
  if (static_cast<double>(rounded) != value) {
    return false;
  }
  outIntegral = rounded;
  return true;
}

bool variantToLong(JsonVariantConst v, long &out) {
  if (v.isNull() || v.is<bool>() || v.is<JsonObjectConst>() || v.is<JsonArrayConst>() ||
      v.is<JsonString>()) {
    return false;
  }
  if (v.is<long>()) {
    out = static_cast<long>(v.as<long>());
    return true;
  }
  if (v.is<int>()) {
    out = static_cast<long>(v.as<int>());
    return true;
  }
  const double d = v.as<double>();
  return roughlyIntegralDouble(d, out);
}

} // namespace

bool loadMetaJson(SdService &sd, JsonDocument &outDoc, const char *path, Print *errOut) {
  String text;
  if (sd.readText(path, text) != ServiceStatus::Ok) {
    if (errOut != nullptr) {
      errOut->print(F("Meta read failed: "));
      errOut->println(path);
    }
    return false;
  }
  outDoc.clear();
  const DeserializationError err = deserializeJson(outDoc, text);
  if (err) {
    if (errOut != nullptr) {
      errOut->print(F("Meta JSON parse error: "));
      errOut->println(err.c_str());
    }
    return false;
  }
  if (!outDoc.is<JsonObject>()) {
    if (errOut != nullptr) {
      errOut->println(F("Meta JSON root must be an object."));
    }
    return false;
  }
  return true;
}

bool metaGetLong(const JsonDocument &doc, const String &dotPath, long &out) {
  bool ok = false;
  const JsonVariantConst v =
      resolveMetaDotPath(doc.as<JsonVariantConst>(), dotPath, &ok);
  if (!ok) {
    return false;
  }
  return variantToLong(v, out);
}

bool metaGetUInt32(const JsonDocument &doc, const String &dotPath, uint32_t &out) {
  long n = 0;
  if (!metaGetLong(doc, dotPath, n) || n < 0 ||
      static_cast<unsigned long>(n) > UINT32_MAX) {
    return false;
  }
  out = static_cast<uint32_t>(n);
  return true;
}

bool metaGetBool(const JsonDocument &doc, const String &dotPath, bool &out) {
  bool ok = false;
  const JsonVariantConst v =
      resolveMetaDotPath(doc.as<JsonVariantConst>(), dotPath, &ok);
  if (!ok || v.isNull() || !v.is<bool>()) {
    return false;
  }
  out = v.as<bool>();
  return true;
}

bool metaGetString(const JsonDocument &doc, const String &dotPath, String &out) {
  bool ok = false;
  const JsonVariantConst v =
      resolveMetaDotPath(doc.as<JsonVariantConst>(), dotPath, &ok);
  if (!ok || v.isNull()) {
    return false;
  }
  if (v.is<JsonObjectConst>() || v.is<JsonArrayConst>() || v.is<bool>()) {
    return false;
  }
  const char *pch = v.as<const char *>();
  if (pch == nullptr) {
    return false;
  }
  out = pch;
  return true;
}

bool metaGetJsonArray(const JsonDocument &doc, const String &dotPath, JsonArrayConst &out) {
  bool ok = false;
  const JsonVariantConst v =
      resolveMetaDotPath(doc.as<JsonVariantConst>(), dotPath, &ok);
  if (!ok || v.isNull() || !v.is<JsonArrayConst>()) {
    return false;
  }
  out = v.as<JsonArrayConst>();
  return true;
}

} // namespace raven
