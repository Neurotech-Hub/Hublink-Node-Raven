#include "LoggerWorkflow.h"

namespace raven
{

  bool shouldRunSyncWindow(uint32_t sleepSeconds, uint32_t syncEverySeconds, uint32_t logCount)
  {
    const uint32_t elapsedSeconds = sleepSeconds * logCount;
    return elapsedSeconds >= syncEverySeconds;
  }

  float computePassesPerMinute(uint16_t passes, uint32_t sleepSeconds)
  {
    if (sleepSeconds == 0)
    {
      return 0.0f;
    }
    return (static_cast<float>(passes) * 60.0f) / static_cast<float>(sleepSeconds);
  }

  bool parseCsvFieldName(const String &fieldText, CsvField &outField)
  {
    String field = fieldText;
    field.toLowerCase();
    field.trim();
    if (field == "millis")
    {
      outField = CsvField::Millis;
    }
    else if (field == "ulp_edges")
    {
      outField = CsvField::UlpEdges;
    }
    else if (field == "magnet_passes")
    {
      outField = CsvField::MagnetPasses;
    }
    else if (field == "passes_min")
    {
      outField = CsvField::PassesPerMin;
    }
    else if (field == "unix")
    {
      outField = CsvField::RtcUnix;
    }
    else if (field == "datetime" || field == "rtc_text")
    {
      outField = CsvField::DateTime;
    }
    else if (field == "rtc_temp_c")
    {
      outField = CsvField::RtcTempC;
    }
    else if (field == "batt_v")
    {
      outField = CsvField::BattV;
    }
    else if (field == "batt_per")
    {
      outField = CsvField::BattPer;
    }
    else if (field == "lux")
    {
      outField = CsvField::Lux;
    }
    else if (field == "als")
    {
      outField = CsvField::Als;
    }
    else if (field == "white")
    {
      outField = CsvField::White;
    }
    else if (field == "temp_c")
    {
      outField = CsvField::TempC;
    }
    else if (field == "pressure_hpa")
    {
      outField = CsvField::PressureHpa;
    }
    else if (field == "humidity_pct")
    {
      outField = CsvField::HumidityPct;
    }
    else if (field == "gas_kohm")
    {
      outField = CsvField::GasKOhm;
    }
    else if (field == "alt_m")
    {
      outField = CsvField::AltM;
    }
    else if (field == "magnet")
    {
      outField = CsvField::Magnet;
    }
    else if (field == "usb_sense")
    {
      outField = CsvField::UsbSense;
    }
    else
    {
      return false;
    }
    return true;
  }

  CsvFieldMask buildCsvFieldMaskFromNames(const String *fieldNames, size_t count,
                                          CsvFieldMask fallbackMask, Print *warnOut)
  {
    CsvFieldMask mask = 0;
    for (size_t i = 0; i < count; ++i)
    {
      CsvField field = CsvField::Millis;
      if (parseCsvFieldName(fieldNames[i], field))
      {
        mask |= csvFieldBit(field);
      }
      else if (warnOut)
      {
        warnOut->print(F("logger.log_fields: unknown field ignored: "));
        warnOut->println(fieldNames[i]);
      }
    }
    return mask == 0 ? fallbackMask : mask;
  }

  ServiceStatus captureAndAppendManagedCsv(DataLoggerHelper &logger, HublinkNode &node,
                                           LogFilePolicy &filePolicy,
                                           CsvFieldMask fieldMask, SampleFields &outSample,
                                           String *outPath)
  {
    outSample = logger.captureSample();

    String logPath;
    const ServiceStatus pathStatus =
        resolveLogFilePath(node.sd(), filePolicy, outSample.rtc, logPath);
    if (pathStatus != ServiceStatus::Ok)
    {
      return pathStatus;
    }

    if (!node.sd().exists(logPath.c_str()))
    {
      const String header =
          fieldMask == 0 ? DataLoggerHelper::csvHeader() : DataLoggerHelper::csvHeader(fieldMask);
      const ServiceStatus headerStatus = node.sd().appendLine(logPath.c_str(), header);
      if (headerStatus != ServiceStatus::Ok)
      {
        return headerStatus;
      }
    }

    const ServiceStatus rowStatus = fieldMask == 0
                                        ? logger.appendCsvSample(logPath.c_str(), outSample)
                                        : logger.appendCsvSample(logPath.c_str(), outSample,
                                                                 fieldMask);
    if (outPath)
    {
      *outPath = logPath;
    }
    return rowStatus;
  }

} // namespace raven
