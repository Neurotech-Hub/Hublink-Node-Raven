#pragma once

#include "../HublinkNode.h"
#include <initializer_list>

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
using SampleFields = CompositeSample;

enum class CsvField : uint8_t {
  Millis = 0,
  UlpEdges,
  MagnetPasses,
  RtcUnix,
  DateTime,
  RtcText = DateTime, // Backward-compatible alias.
  RtcTempC,
  BattV,
  BattPer,
  Lux,
  Als,
  White,
  TempC,
  PressureHpa,
  HumidityPct,
  GasKOhm,
  AltM,
  Magnet,
  UsbSense,
};

using CsvFieldMask = uint32_t;

constexpr CsvFieldMask csvFieldBit(CsvField field) {
  return static_cast<CsvFieldMask>(1UL << static_cast<uint8_t>(field));
}

inline CsvFieldMask csvFields(std::initializer_list<CsvField> fields) {
  CsvFieldMask mask = 0;
  for (CsvField field : fields) {
    mask |= csvFieldBit(field);
  }
  return mask;
}

class DataLoggerHelper {
public:
  explicit DataLoggerHelper(HublinkNode &node) : node_(node) {}

  bool begin();
  CompositeSample captureSample();
  ServiceStatus appendCsvSample(const char *path, const CompositeSample &sample);
  ServiceStatus appendCsvSample(const char *path, const CompositeSample &sample,
                                CsvFieldMask mask);
  static String csvHeader();
  static String csvHeader(CsvFieldMask mask);
  static String toCsv(const CompositeSample &sample);
  static String toCsv(const CompositeSample &sample, CsvFieldMask mask);

private:
  HublinkNode &node_;
};

} // namespace raven
