#include "MetaJsonPath.h"

namespace raven {

JsonVariantConst resolveMetaDotPath(JsonVariantConst root, const String &path,
                                    bool *ok) {
  if (ok != nullptr) {
    *ok = false;
  }
  if (path.length() == 0) {
    return JsonVariantConst();
  }

  JsonVariantConst current = root;
  if (!current.is<JsonObjectConst>()) {
    return JsonVariantConst();
  }

  int start = 0;
  while (true) {
    const int dot = path.indexOf('.', start);
    const String token = dot < 0 ? path.substring(start) : path.substring(start, dot);
    if (token.length() == 0) {
      return JsonVariantConst();
    }

    JsonObjectConst obj = current.as<JsonObjectConst>();
    if (!obj.containsKey(token.c_str())) {
      return JsonVariantConst();
    }
    const JsonVariantConst next = obj[token.c_str()];

    if (dot < 0) {
      if (ok != nullptr) {
        *ok = true;
      }
      return next;
    }

    if (!next.is<JsonObjectConst>()) {
      return JsonVariantConst();
    }

    current = next;
    start = dot + 1;
  }
}

} // namespace raven
