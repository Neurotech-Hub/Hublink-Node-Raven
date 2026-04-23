#include "DataLoggerHelper.h"

namespace raven {

bool DataLoggerHelper::begin() {
  if (!node_.beginHardware()) {
    return false;
  }
  if (!node_.beginI2C()) {
    return false;
  }

  // Allow partial peripheral availability; each read reports its own status.
  node_.rtc().begin();
  node_.powerGauge().begin();
  node_.light().begin();
  node_.environment().begin();
  node_.sd().begin();
  return true;
}

CompositeSample DataLoggerHelper::captureSample() {
  CompositeSample sample;
  sample.millisStamp = millis();
  sample.ulpEdgeCount = node_.magnetCounter().edgeCount();
  sample.magnetPassCount = node_.magnetCounter().magnetPassCount();
  sample.rtc = node_.rtc().readSample();
  sample.battery = node_.powerGauge().readSample();
  sample.light = node_.light().readSample();
  sample.environment = node_.environment().readSample();
  sample.magnet = node_.readMagnet();
  sample.usbSense = node_.readUsbSense();
  return sample;
}

ServiceStatus DataLoggerHelper::appendCsvSample(const char *path,
                                                const CompositeSample &sample) {
  return node_.sd().appendLine(path, toCsv(sample));
}

String DataLoggerHelper::csvHeader() {
  return String(F("millis,ulp_edges,magnet_passes,rtc_unix,rtc_temp_c,batt_v,batt_soc,lux,als,white,temp_c,pressure_hpa,humidity_pct,gas_kohm,alt_m,magnet,usb_sense"));
}

String DataLoggerHelper::toCsv(const CompositeSample &sample) {
  String line;
  line.reserve(200);
  line += String(sample.millisStamp);
  line += ",";
  line += String(sample.ulpEdgeCount);
  line += ",";
  line += String(sample.magnetPassCount);
  line += ",";
  line += sample.rtc.status == ServiceStatus::Ok ? String(sample.rtc.now.unixtime()) : "";
  line += ",";
  line += sample.rtc.status == ServiceStatus::Ok ? String(sample.rtc.temperatureC, 2) : "";
  line += ",";
  line += sample.battery.status == ServiceStatus::Ok ? String(sample.battery.voltageV, 3) : "";
  line += ",";
  line += sample.battery.status == ServiceStatus::Ok ? String(sample.battery.stateOfChargePct, 1) : "";
  line += ",";
  line += sample.light.status == ServiceStatus::Ok ? String(sample.light.lux, 2) : "";
  line += ",";
  line += sample.light.status == ServiceStatus::Ok ? String(sample.light.als) : "";
  line += ",";
  line += sample.light.status == ServiceStatus::Ok ? String(sample.light.white) : "";
  line += ",";
  line += sample.environment.status == ServiceStatus::Ok ? String(sample.environment.temperatureC, 2) : "";
  line += ",";
  line += sample.environment.status == ServiceStatus::Ok ? String(sample.environment.pressureHpa, 2) : "";
  line += ",";
  line += sample.environment.status == ServiceStatus::Ok ? String(sample.environment.humidityPct, 2) : "";
  line += ",";
  line += sample.environment.status == ServiceStatus::Ok ? String(sample.environment.gasKOhms, 2) : "";
  line += ",";
  line += sample.environment.status == ServiceStatus::Ok ? String(sample.environment.altitudeM, 2) : "";
  line += ",";
  line += sample.magnet ? "1" : "0";
  line += ",";
  line += sample.usbSense ? "1" : "0";
  return line;
}

} // namespace raven
