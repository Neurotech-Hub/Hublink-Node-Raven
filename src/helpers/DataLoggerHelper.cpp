#include "DataLoggerHelper.h"
#include "LowBatteryBoot.h"

namespace raven
{
  namespace
  {

    CsvFieldMask allCsvFieldMask()
    {
      return csvFields({
          CsvField::Millis,
          CsvField::UlpEdges,
          CsvField::MagnetPasses,
          CsvField::PassesPerMin,
          CsvField::RtcUnix,
          CsvField::DateTime,
          CsvField::RtcTempC,
          CsvField::BattV,
          CsvField::BattPer,
          CsvField::Lux,
          CsvField::Als,
          CsvField::White,
          CsvField::TempC,
          CsvField::PressureHpa,
          CsvField::HumidityPct,
          CsvField::GasKOhm,
          CsvField::AltM,
          CsvField::Magnet,
          CsvField::UsbSense,
      });
    }

    String valueForField(CsvField field, const CompositeSample &sample)
    {
      switch (field)
      {
      case CsvField::Millis:
        return String(sample.millisStamp);
      case CsvField::UlpEdges:
        return String(sample.ulpEdgeCount);
      case CsvField::MagnetPasses:
        return String(sample.magnetPassCount);
      case CsvField::PassesPerMin:
        return String(sample.passesPerMin, 2);
      case CsvField::RtcUnix:
        return sample.rtc.status == ServiceStatus::Ok ? String(sample.rtc.now.unixtime()) : "";
      case CsvField::DateTime:
        if (sample.rtc.status == ServiceStatus::Ok && sample.rtc.now.isValid())
        {
          char datetime[20];
          snprintf(datetime, sizeof(datetime), "%04d-%02d-%02d %02d:%02d:%02d",
                   sample.rtc.now.year(), sample.rtc.now.month(), sample.rtc.now.day(),
                   sample.rtc.now.hour(), sample.rtc.now.minute(), sample.rtc.now.second());
          return String(datetime);
        }
        return "";
      case CsvField::RtcTempC:
        return sample.rtc.status == ServiceStatus::Ok ? String(sample.rtc.temperatureC, 2) : "";
      case CsvField::BattV:
        if (sample.battery.status == ServiceStatus::Ok && sample.battery.hasCellReading)
        {
          return String(sample.battery.voltageV, 3);
        }
        return "0";
      case CsvField::BattPer:
        if (sample.battery.status == ServiceStatus::Ok && sample.battery.hasCellReading)
        {
          return String(sample.battery.stateOfChargePct, 1);
        }
        return "0";
      case CsvField::Lux:
        return sample.light.status == ServiceStatus::Ok ? String(sample.light.lux, 2) : "";
      case CsvField::Als:
        return sample.light.status == ServiceStatus::Ok ? String(sample.light.als) : "";
      case CsvField::White:
        return sample.light.status == ServiceStatus::Ok ? String(sample.light.white) : "";
      case CsvField::TempC:
        return sample.environment.status == ServiceStatus::Ok
                   ? String(sample.environment.temperatureC, 2)
                   : "";
      case CsvField::PressureHpa:
        return sample.environment.status == ServiceStatus::Ok
                   ? String(sample.environment.pressureHpa, 2)
                   : "";
      case CsvField::HumidityPct:
        return sample.environment.status == ServiceStatus::Ok
                   ? String(sample.environment.humidityPct, 2)
                   : "";
      case CsvField::GasKOhm:
        return sample.environment.status == ServiceStatus::Ok
                   ? String(sample.environment.gasKOhms, 2)
                   : "";
      case CsvField::AltM:
        return sample.environment.status == ServiceStatus::Ok
                   ? String(sample.environment.altitudeM, 2)
                   : "";
      case CsvField::Magnet:
        return sample.magnet ? "1" : "0";
      case CsvField::UsbSense:
        return sample.usbSense ? "1" : "0";
      }
      return "";
    }

    const __FlashStringHelper *nameForField(CsvField field)
    {
      switch (field)
      {
      case CsvField::Millis:
        return F("millis");
      case CsvField::UlpEdges:
        return F("ulp_edges");
      case CsvField::MagnetPasses:
        return F("magnet_passes");
      case CsvField::PassesPerMin:
        return F("passes_min");
      case CsvField::RtcUnix:
        return F("unix");
      case CsvField::DateTime:
        return F("datetime");
      case CsvField::RtcTempC:
        return F("rtc_temp_c");
      case CsvField::BattV:
        return F("batt_v");
      case CsvField::BattPer:
        return F("batt_per");
      case CsvField::Lux:
        return F("lux");
      case CsvField::Als:
        return F("als");
      case CsvField::White:
        return F("white");
      case CsvField::TempC:
        return F("temp_c");
      case CsvField::PressureHpa:
        return F("pressure_hpa");
      case CsvField::HumidityPct:
        return F("humidity_pct");
      case CsvField::GasKOhm:
        return F("gas_kohm");
      case CsvField::AltM:
        return F("alt_m");
      case CsvField::Magnet:
        return F("magnet");
      case CsvField::UsbSense:
        return F("usb_sense");
      }
      return F("");
    }

    template <typename ValueBuilder>
    String buildCsv(CsvFieldMask mask, ValueBuilder valueBuilder)
    {
      String line;
      bool first = true;
      for (uint8_t bit = 0; bit <= static_cast<uint8_t>(CsvField::UsbSense); ++bit)
      {
        const CsvField field = static_cast<CsvField>(bit);
        const CsvFieldMask bitMask = csvFieldBit(field);
        if ((mask & bitMask) == 0)
        {
          continue;
        }
        if (!first)
        {
          line += ",";
        }
        line += valueBuilder(field);
        first = false;
      }
      return line;
    }

  } // namespace

  bool DataLoggerHelper::begin()
  {
    if (!node_.beginHardware())
    {
      return false;
    }
    if (!node_.beginI2C())
    {
      return false;
    }

    maybeAutomaticVoltageSafeguard(node_, true);

    // Allow partial peripheral availability; each read reports its own status.
    node_.rtc().begin();
    node_.powerGauge().begin();
    node_.light().begin();
    node_.environment().begin();
    node_.sd().begin();
    return true;
  }

  CompositeSample DataLoggerHelper::captureSample()
  {
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
                                                  const CompositeSample &sample)
  {
    return appendCsvSample(path, sample, allCsvFieldMask());
  }

  ServiceStatus DataLoggerHelper::appendCsvSample(const char *path,
                                                  const CompositeSample &sample,
                                                  CsvFieldMask mask)
  {
    const CsvFieldMask selectedMask = mask & allCsvFieldMask();
    if (selectedMask == 0)
    {
      return ServiceStatus::InvalidData;
    }
    return node_.sd().appendLine(path, toCsv(sample, selectedMask));
  }

  String DataLoggerHelper::csvHeader()
  {
    return csvHeader(allCsvFieldMask());
  }

  String DataLoggerHelper::csvHeader(CsvFieldMask mask)
  {
    const CsvFieldMask selectedMask = mask & allCsvFieldMask();
    if (selectedMask == 0)
    {
      return "";
    }
    return buildCsv(selectedMask, [](CsvField field)
                    { return String(nameForField(field)); });
  }

  String DataLoggerHelper::toCsv(const CompositeSample &sample)
  {
    return toCsv(sample, allCsvFieldMask());
  }

  String DataLoggerHelper::toCsv(const CompositeSample &sample, CsvFieldMask mask)
  {
    const CsvFieldMask selectedMask = mask & allCsvFieldMask();
    if (selectedMask == 0)
    {
      return "";
    }
    return buildCsv(selectedMask, [&sample](CsvField field)
                    { return valueForField(field, sample); });
  }

} // namespace raven
