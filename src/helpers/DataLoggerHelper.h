#pragma once

#include "../HublinkNode.h"

namespace raven {

struct CompositeSample {
  uint32_t millisStamp = 0;
  uint16_t ulpEdgeCount = 0;
  uint16_t magnetPassCount = 0;
  RtcReading rtc;
  BatteryReading battery;
  LightReading light;
  EnvReading environment;
  bool magnet = false;
  bool usbSense = false;
};

class DataLoggerHelper {
public:
  explicit DataLoggerHelper(HublinkNode &node) : node_(node) {}

  bool begin();
  CompositeSample captureSample();
  ServiceStatus appendCsvSample(const char *path, const CompositeSample &sample);
  static String csvHeader();
  static String toCsv(const CompositeSample &sample);

private:
  HublinkNode &node_;
};

} // namespace raven
