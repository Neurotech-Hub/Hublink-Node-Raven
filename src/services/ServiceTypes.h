#pragma once

#include <Arduino.h>

namespace raven {

enum class ServiceStatus : uint8_t {
  Ok = 0,
  NotInitialized,
  NotFound,
  ReadFailed,
  WriteFailed,
  InvalidData,
};

inline const __FlashStringHelper *statusToString(ServiceStatus status) {
  switch (status) {
  case ServiceStatus::Ok:
    return F("ok");
  case ServiceStatus::NotInitialized:
    return F("not_initialized");
  case ServiceStatus::NotFound:
    return F("not_found");
  case ServiceStatus::ReadFailed:
    return F("read_failed");
  case ServiceStatus::WriteFailed:
    return F("write_failed");
  case ServiceStatus::InvalidData:
    return F("invalid_data");
  }
  return F("unknown");
}

} // namespace raven
