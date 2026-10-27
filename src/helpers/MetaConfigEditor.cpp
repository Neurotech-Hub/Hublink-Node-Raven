#include "MetaConfigEditor.h"
#include "../HublinkNode.h"
#include "LowBatteryBoot.h"
#include "MetaConfigReader.h"
#include "MetaJsonPath.h"
#include "RtcWallTime.h"
#include <SD.h>
#include <stdio.h>

namespace raven
{

  namespace
  {
    bool isIntText(const String &text)
    {
      if (text.length() == 0)
      {
        return false;
      }
      size_t i = 0;
      if (text[0] == '-' || text[0] == '+')
      {
        i = 1;
      }
      if (i >= text.length())
      {
        return false;
      }
      for (; i < text.length(); ++i)
      {
        if (text[i] < '0' || text[i] > '9')
        {
          return false;
        }
      }
      return true;
    }

    bool isFloatText(const String &text)
    {
      bool seenDot = false;
      bool seenDigit = false;
      size_t i = 0;
      if (text.length() == 0)
      {
        return false;
      }
      if (text[0] == '-' || text[0] == '+')
      {
        i = 1;
      }
      for (; i < text.length(); ++i)
      {
        const char c = text[i];
        if (c >= '0' && c <= '9')
        {
          seenDigit = true;
          continue;
        }
        if (c == '.' && !seenDot)
        {
          seenDot = true;
          continue;
        }
        return false;
      }
      return seenDigit && seenDot;
    }

    void ensureNodeSensorsReady(HublinkNode &node)
    {
      // Do not call beginHardware() unconditionally while the editor has SD mounted:
      // beginI2C() runs beginHardware() if needed, and setI2CPowerEnabled(true)
      // ensures the aux I2C rail is on without disturbing SPI or PIN_SD_EN.
      node.setI2CPowerEnabled(true);
      node.beginI2C();
      node.rtc().begin();
      node.powerGauge().begin();
      node.light().begin();
      node.environment().begin();
    }

    void printRtcLine(Stream &io, const char *label, const RtcReading &r)
    {
      io.print(label);
      io.print(F(": "));
      io.print(statusToString(r.status));
      if (r.status == ServiceStatus::Ok && r.now.isValid())
      {
        char dt[24];
        snprintf(dt, sizeof(dt), "%04u-%02u-%02u %02u:%02u:%02u", r.now.year(), r.now.month(),
                 r.now.day(), r.now.hour(), r.now.minute(), r.now.second());
        io.print(F(" "));
        io.print(dt);
        if (!isnan(r.temperatureC))
        {
          io.print(F(" rtcTemp="));
          io.print(r.temperatureC, 1);
          io.print('C');
        }
      }
      io.println();
    }

    void printFuelLine(Stream &io, const char *label, HublinkNode &node, const BatteryReading &b,
                       bool verbose)
    {
      io.print(label);
      io.print(F(": "));
      io.print(statusToString(b.status));
      if (b.hasCellReading && b.status == ServiceStatus::Ok)
      {
        io.print(F(" V="));
        io.print(b.voltageV, 3);
        io.print(F(" SOC="));
        io.print(b.stateOfChargePct, 1);
        io.print('%');
      }
      if (node.powerGauge().isInitialized())
      {
        io.print(F(" chip=0x"));
        if (node.powerGauge().chipId() < 16U)
        {
          io.print('0');
        }
        io.print(node.powerGauge().chipId(), HEX);
      }
      if (verbose)
      {
        io.print(F(" init="));
        io.print(node.powerGauge().isInitialized() ? F("y") : F("n"));
      }
      io.println();
    }

    void printLightLine(Stream &io, const char *label, const LightReading &l)
    {
      io.print(label);
      io.print(F(": "));
      io.print(statusToString(l.status));
      if (l.status == ServiceStatus::Ok && !isnan(l.lux))
      {
        io.print(F(" lux="));
        io.print(l.lux, 2);
        io.print(F(" als="));
        io.print(l.als);
      }
      io.println();
    }

    void printEnvLine(Stream &io, const char *label, const EnvReading &e)
    {
      io.print(label);
      io.print(F(": "));
      io.print(statusToString(e.status));
      if (e.status == ServiceStatus::Ok)
      {
        if (!isnan(e.temperatureC))
        {
          io.print(F(" T="));
          io.print(e.temperatureC, 1);
          io.print('C');
        }
        if (!isnan(e.pressureHpa))
        {
          io.print(F(" P="));
          io.print(e.pressureHpa, 1);
          io.print(F("hPa"));
        }
        if (!isnan(e.humidityPct))
        {
          io.print(F(" RH="));
          io.print(e.humidityPct, 0);
          io.print('%');
        }
      }
      io.println();
    }

    void printPinsLine(Stream &io, HublinkNode &node)
    {
      io.print(F("pins: magnet="));
      io.print(node.readMagnet() ? F("HIGH") : F("LOW"));
      io.print(F(" usb="));
      io.println(node.readUsbSense() ? F("present") : F("absent"));
    }

    // Assign through JsonObject. A JsonVariant copied out of operator[] does not
    // create missing keys, and set() on that copy leaves the document unchanged.
    bool assignRtcMetaFields(JsonDocument &doc, const char *datetime, const char *zone)
    {
      if (datetime == nullptr || zone == nullptr || !doc.is<JsonObject>())
      {
        return false;
      }
      JsonObject root = doc.as<JsonObject>();
      if (!root["rtc"].is<JsonObject>())
      {
        root["rtc"].to<JsonObject>();
      }
      JsonObject rtc = root["rtc"].as<JsonObject>();
      if (rtc.isNull())
      {
        return false;
      }
      if (!rtc["datetime"].set(datetime) || !rtc["timezone"].set(zone))
      {
        return false;
      }
      return !doc.overflowed();
    }

  } // namespace

  bool MetaConfigEditor::maybeEnter(SdService &sd, bool usbPresent, Stream &io, uint32_t holdWindowMs,
                                    HublinkNode *node)
  {
    if (!usbPresent)
    {
      return false;
    }
    if (!sd.begin() || sd.cardType() == CARD_NONE)
    {
      io.println(F("Meta editor unavailable: SD card not mounted."));
      return false;
    }

    io.println();
    io.print(F("Press 'e' within "));
    io.print(holdWindowMs / 1000);
    io.println(F("s to enter meta editor."));
    const uint32_t startMs = millis();
    while ((millis() - startMs) < holdWindowMs)
    {
      if (io.available())
      {
        const char c = static_cast<char>(io.read());
        if (c == 'e' || c == 'E')
        {
          io.println(F("Entering meta editor..."));
          runShell(sd, io, node);
          return true;
        }
        break;
      }
      delay(10);
    }
    return false;
  }

  bool MetaConfigEditor::maybeEnterWithFade(SdService &sd, bool usbPresent, Stream &io,
                                            uint32_t holdWindowMs, uint8_t ledPrimaryPin,
                                            uint8_t ledSecondaryPin, HublinkNode *node)
  {
    if (!usbPresent)
    {
      return false;
    }
    if (!sd.begin() || sd.cardType() == CARD_NONE)
    {
      io.println(F("Meta editor unavailable: SD card not mounted."));
      return false;
    }

    io.println(F("Press 'e' within 3s to enter meta editor."));
    pinMode(ledPrimaryPin, OUTPUT);
    pinMode(ledSecondaryPin, OUTPUT);
    const uint32_t startMs = millis();
    int duty = 0;
    int step = 12;
    while ((millis() - startMs) < holdWindowMs)
    {
      if (io.available())
      {
        const char c = static_cast<char>(io.read());
        if (c == 'e' || c == 'E')
        {
          // Drain trailing CR/LF from serial monitor line endings so the
          // first editor prompt doesn't immediately consume an empty command.
          while (io.available())
          {
            const char trailing = static_cast<char>(io.read());
            if (trailing != '\r' && trailing != '\n')
            {
              break;
            }
          }
          analogWrite(ledPrimaryPin, 255);
          analogWrite(ledSecondaryPin, 255);
          return enterNow(sd, io, node);
        }
      }

      analogWrite(ledPrimaryPin, duty);
      analogWrite(ledSecondaryPin, duty);
      duty += step;
      if (duty >= 255)
      {
        duty = 255;
        step = -step;
      }
      else if (duty <= 0)
      {
        duty = 0;
        step = -step;
      }
      delay(25);
    }

    analogWrite(ledPrimaryPin, 255);
    analogWrite(ledSecondaryPin, 0);
    return false;
  }

  bool MetaConfigEditor::enterNow(SdService &sd, Stream &io, HublinkNode *node)
  {
    if (!sd.begin() || sd.cardType() == CARD_NONE)
    {
      io.println(F("Meta editor unavailable: SD card not mounted."));
      return false;
    }
    io.println(F("Entering meta editor..."));
    return runShell(sd, io, node);
  }

  bool MetaConfigEditor::runShell(SdService &sd, Stream &io, HublinkNode *node)
  {
    JsonDocument doc;
    if (!loadMetaDoc(sd, doc, io))
    {
      return false;
    }

    printTopHelp(io);
    bool shouldExit = false;
    while (!shouldExit)
    {
      io.print(F("meta> "));
      String line;
      if (!readLine(io, line))
      {
        continue;
      }
      line.trim();
      if (line.length() == 0)
      {
        continue;
      }
      if (line == "exit")
      {
        shouldExit = true;
        continue;
      }
      if (line == "help")
      {
        printTopHelp(io);
        continue;
      }
      if (line == "reboot")
      {
        io.println(F("Rebooting now..."));
        delay(100);
        ESP.restart();
      }

      const int firstSpace = line.indexOf(' ');
      const String rootCmd = firstSpace < 0 ? line : line.substring(0, firstSpace);
      const String rest = firstSpace < 0 ? String("") : trimCopy(line.substring(firstSpace + 1));

      if (rootCmd == "meta")
      {
        if (!cmdMeta(io, sd, rest))
        {
          // no-op
        }
        else if (rest.startsWith("reload"))
        {
          loadMetaDoc(sd, doc, io);
        }
        else if (rest.startsWith("save"))
        {
          saveMetaDocAtomic(sd, doc, io);
        }
        else
        {
          const String op = trimCopy(rest);
          if (op.startsWith("get "))
          {
            metaGet(doc, trimCopy(op.substring(4)), io);
          }
          else if (op.startsWith("setjson "))
          {
            String path;
            String jsonText;
            if (splitTwoArgs(trimCopy(op.substring(8)), path, jsonText))
            {
              metaSetJson(doc, path, jsonText, io);
            }
            else
            {
              io.println(F("Usage: meta setjson <json.path> <json_literal>"));
            }
          }
          else if (op.startsWith("set "))
          {
            String path;
            String value;
            if (splitTwoArgs(trimCopy(op.substring(4)), path, value))
            {
              metaSet(doc, path, value, io);
            }
            else
            {
              io.println(F("Usage: meta set <json.path> <value>"));
            }
          }
          else if (op.startsWith("del "))
          {
            metaDel(doc, trimCopy(op.substring(4)), io);
          }
          else if (op == "show")
          {
            serializeJsonPretty(doc, io);
            io.println();
          }
        }
        continue;
      }

      if (rootCmd == "file")
      {
        cmdFile(io, sd, rest);
        continue;
      }

      if (rootCmd == "sensor")
      {
        cmdSensor(io, node, rest);
        continue;
      }

      if (rootCmd == "rtc")
      {
        cmdRtc(io, sd, node, doc, rest);
        continue;
      }

      io.println(F("Unknown command. Type: help"));
    }

    io.println(F("Leaving meta editor."));
    return true;
  }

  bool MetaConfigEditor::readLine(Stream &io, String &lineOut)
  {
    lineOut = "";
    while (true)
    {
      if (!io.available())
      {
        delay(5);
        continue;
      }
      const char c = static_cast<char>(io.read());
      if (c == '\r')
      {
        continue;
      }
      if (c == '\n')
      {
        return true;
      }
      lineOut += c;
      if (lineOut.length() > kMaxInputLine)
      {
        io.println(F("Line too long."));
        return false;
      }
    }
  }

  bool MetaConfigEditor::handleCommand(SdService &, Stream &, const String &, bool &) { return true; }

  void MetaConfigEditor::printTopHelp(Stream &io) const
  {
    io.println(F("Commands:"));
    io.println(F("  help"));
    io.println(F("  reboot"));
    io.println(F("  meta help | meta show | meta get <path>"));
    io.println(F("  meta set <path> <value>"));
    io.println(F("  meta setjson <path> <json_literal>"));
    io.println(F("  meta del <path> | meta save | meta reload"));
    io.println(F("  file help | file list | file cat <name> | file rm <n> | file rm all"));
    io.println(F("  sensor help | sensor | sensor list | sensor fuel | sensor safeguard | ..."));
    io.println(F("  rtc | rtc set <YYYY-MM-DD> <HH:MM:SS> <±HH:MM|Z>"));
    io.println(F("  exit"));
  }

  void MetaConfigEditor::printMetaHelp(Stream &io) const
  {
    io.println(F("Meta commands:"));
    io.println(F("  meta show"));
    io.println(F("  meta get wheel.sleep_time_seconds"));
    io.println(F("  meta set wheel.sleep_time_seconds 15"));
    io.println(F("  meta set logger.inc_on_reboot true"));
    io.println(F("  meta setjson logger.log_fields [\"unix\",\"datetime\"]"));
    io.println(F("  meta del logger.log_fields"));
    io.println(F("  meta save"));
    io.println(F("  meta reload"));
  }

  void MetaConfigEditor::printFileHelp(Stream &io) const
  {
    io.println(F("File commands (root only):"));
    io.println(F("  file list"));
    io.println(F("  file cat <name>"));
    io.println(F("  file rm <n>"));
    io.println(F("  file rm all"));
  }

  bool MetaConfigEditor::cmdMeta(Stream &io, SdService &sd, const String &rest)
  {
    const String op = trimCopy(rest);
    if (op == "help")
    {
      printMetaHelp(io);
      return true;
    }
    if (op == "reload" || op == "save" || op == "show" || op.startsWith("get ") ||
        op.startsWith("set ") || op.startsWith("setjson ") || op.startsWith("del "))
    {
      return true;
    }
    io.println(F("Unknown meta command. Use: meta help"));
    return false;
  }

  bool MetaConfigEditor::cmdFile(Stream &io, SdService &sd, const String &rest)
  {
    const String op = trimCopy(rest);
    if (op == "help")
    {
      printFileHelp(io);
      return true;
    }
    if (op == "list")
    {
      return fileList(io);
    }
    if (op.startsWith("cat "))
    {
      return fileCat(sd, io, trimCopy(op.substring(4)));
    }
    if (op.startsWith("rm "))
    {
      const String rmArg = trimCopy(op.substring(3));
      if (rmArg == "all")
      {
        return fileRmAll(sd, io);
      }
      return fileRm(sd, io, rmArg);
    }
    io.println(F("Unknown file command. Use: file help"));
    return false;
  }

  void MetaConfigEditor::printSensorHelp(Stream &io) const
  {
    io.println(F("Sensor commands (need HublinkNode when opening editor):"));
    io.println(F("  sensor          — one-shot: rtc, fuel, light, env, pins"));
    io.println(F("  sensor all      — same as sensor"));
    io.println(F("  sensor list     — status line per device (quick)"));
    io.println(F("  sensor fuel     — battery gauge (aliases: batt, battery)"));
    io.println(F("  sensor rtc | light | env | pins"));
    io.println(F("  sensor safeguard — low-voltage safeguard diagnose (no sleep)"));
  }

  bool MetaConfigEditor::cmdSensor(Stream &io, HublinkNode *node, const String &rest)
  {
    if (node == nullptr)
    {
      io.println(
          F("sensor: no HublinkNode (pass node into maybeEnterWithFade / enterNow for readings)."));
      return false;
    }

    const String op = trimCopy(rest);
    if (op == "help")
    {
      printSensorHelp(io);
      return true;
    }

    ensureNodeSensorsReady(*node);

    if (op == "list")
    {
      const RtcReading rtc = node->rtc().readSample();
      io.print(F("rtc: "));
      io.println(statusToString(rtc.status));

      const BatteryReading bat = node->powerGauge().readSample();
      io.print(F("fuel: "));
      io.print(statusToString(bat.status));
      if (node->powerGauge().isInitialized())
      {
        io.print(F(" chip=0x"));
        if (node->powerGauge().chipId() < 16U)
        {
          io.print('0');
        }
        io.print(node->powerGauge().chipId(), HEX);
      }
      io.println();

      const LightReading light = node->light().readSample();
      io.print(F("light: "));
      io.println(statusToString(light.status));

      const EnvReading env = node->environment().readSample();
      io.print(F("env: "));
      io.println(statusToString(env.status));

      printPinsLine(io, *node);
      return true;
    }

    if (op == "fuel" || op == "batt" || op == "battery")
    {
      const BatteryReading bat = node->powerGauge().readSample();
      printFuelLine(io, "fuel", *node, bat, true);
      return true;
    }

    if (op == "rtc")
    {
      const RtcReading rtc = node->rtc().readSample();
      printRtcLine(io, "rtc", rtc);
      return true;
    }

    if (op == "light")
    {
      const LightReading light = node->light().readSample();
      printLightLine(io, "light", light);
      return true;
    }

    if (op == "env")
    {
      const EnvReading env = node->environment().readSample();
      printEnvLine(io, "env", env);
      return true;
    }

    if (op == "pins")
    {
      printPinsLine(io, *node);
      return true;
    }

    if (op == "safeguard")
    {
      (void)diagnoseVoltageSafeguard(io, *node, node->readUsbSense());
      return true;
    }

    if (op.length() == 0 || op == "all")
    {
      const RtcReading rtc = node->rtc().readSample();
      printRtcLine(io, "rtc", rtc);

      const BatteryReading bat = node->powerGauge().readSample();
      printFuelLine(io, "fuel", *node, bat, false);

      const LightReading light = node->light().readSample();
      printLightLine(io, "light", light);

      const EnvReading env = node->environment().readSample();
      printEnvLine(io, "env", env);

      printPinsLine(io, *node);
      return true;
    }

    io.println(F("Unknown sensor subcommand. Try: sensor help"));
    return false;
  }

  void MetaConfigEditor::printRtcHelp(Stream &io) const
  {
    io.println(F("RTC commands (need HublinkNode when opening editor):"));
    io.println(F("  rtc"));
    io.println(F("  rtc set 2026-10-26 13:19:00 -05:00"));
    io.println(F("  rtc set 2026-10-26 13:19:00 Z"));
    io.println(F("Writes that wall-clock time into the DS3231 and saves"));
    io.println(F("rtc.datetime and rtc.timezone in /meta.json."));
    io.println(F("The offset is a label; it is not added to the clock."));
  }

  bool MetaConfigEditor::cmdRtc(Stream &io, SdService &sd, HublinkNode *node, JsonDocument &doc,
                                const String &rest)
  {
    const String op = trimCopy(rest);
    if (op == "help")
    {
      printRtcHelp(io);
      return true;
    }
    if (node == nullptr)
    {
      io.println(F("rtc: no HublinkNode (pass node into maybeEnterWithFade / enterNow)."));
      return false;
    }

    ensureNodeSensorsReady(*node);

    if (op.length() == 0)
    {
      const RtcReading rtc = node->rtc().readSample();
      printRtcLine(io, "rtc", rtc);
      io.print(F("oscillatorStopped="));
      io.println(rtc.hadLostPower ? F("yes") : F("no"));

      String savedDt;
      String savedTz;
      if (metaGetString(doc, String("rtc.datetime"), savedDt))
      {
        io.print(F("meta rtc.datetime="));
        io.println(savedDt);
      }
      else
      {
        io.println(F("meta rtc.datetime=(unset)"));
      }
      if (metaGetString(doc, String("rtc.timezone"), savedTz))
      {
        io.print(F("meta rtc.timezone="));
        io.println(savedTz);
      }
      else
      {
        io.println(F("meta rtc.timezone=(unset)"));
      }
      return true;
    }

    if (op != "set" && !op.startsWith("set "))
    {
      io.println(F("Unknown rtc command. Try: rtc help"));
      return false;
    }

    String dateText;
    String timeAndZone;
    String timeText;
    String zoneText;
    const String args = op == "set" ? String("") : trimCopy(op.substring(4));
    if (!splitTwoArgs(args, dateText, timeAndZone) || !splitTwoArgs(timeAndZone, timeText, zoneText))
    {
      io.println(F("Usage: rtc set <YYYY-MM-DD> <HH:MM:SS> <±HH:MM|Z>"));
      return false;
    }

    DateTime wall;
    const String wallText = dateText + " " + timeText;
    if (!parseWallDateTime(wallText.c_str(), wall))
    {
      io.println(F("Invalid date-time. Use YYYY-MM-DD HH:MM:SS."));
      return false;
    }
    if (!isTimezoneOffsetLabel(zoneText.c_str()))
    {
      io.println(F("Invalid timezone label. Use Z or ±HH:MM (hours 00-14)."));
      return false;
    }

    char stored[20];
    char zoneStored[8];
    snprintf(stored, sizeof(stored), "%04u-%02u-%02u %02u:%02u:%02u",
             static_cast<unsigned>(wall.year()), static_cast<unsigned>(wall.month()),
             static_cast<unsigned>(wall.day()), static_cast<unsigned>(wall.hour()),
             static_cast<unsigned>(wall.minute()), static_cast<unsigned>(wall.second()));
    snprintf(zoneStored, sizeof(zoneStored), "%s", (zoneText == "z") ? "Z" : zoneText.c_str());

    // Chip first, and no Serial.flush(): HWCDC flush can wait forever while the
    // COM port stays open, so the editor never reads another command.
    io.println(F("Setting DS3231..."));
    if (!node->rtc().adjust(wall))
    {
      io.println(F("RTC adjust failed. meta.json was not changed."));
      return false;
    }
    io.print(F("RTC set to "));
    io.print(stored);
    io.print(F(" timezone "));
    io.println(zoneStored);

    if (!assignRtcMetaFields(doc, stored, zoneStored))
    {
      io.println(F("RTC chip was updated; could not update rtc fields in meta.json."));
      return false;
    }
    io.println(F("Saving /meta.json..."));
    if (!saveMetaDocAtomic(sd, doc, io))
    {
      io.println(F("RTC chip was updated; /meta.json was not saved."));
      return false;
    }
    return true;
  }

  bool MetaConfigEditor::loadMetaDoc(SdService &sd, JsonDocument &doc, Stream &io)
  {
    return loadMetaJson(sd, doc, kMetaPath, &io);
  }

  bool MetaConfigEditor::saveMetaDocAtomic(SdService &sd, const JsonDocument &doc, Stream &io)
  {
    String body;
    if (serializeJsonPretty(doc, body) == 0 || body.length() == 0)
    {
      io.println(F("Failed to format /meta.json."));
      return false;
    }
    if (body.charAt(body.length() - 1) != '\n')
    {
      body += '\n';
    }

    if (!sd.begin())
    {
      io.println(F("SD not mounted."));
      return false;
    }
    SD.remove(kTempPath);
    File out = SD.open(kTempPath, FILE_WRITE);
    if (!out)
    {
      io.println(F("Failed to open /meta.tmp"));
      return false;
    }
    const size_t written =
        out.write(reinterpret_cast<const uint8_t *>(body.c_str()), body.length());
    out.close();
    if (written != body.length())
    {
      io.println(F("Failed to write /meta.tmp"));
      SD.remove(kTempPath);
      return false;
    }

    String verifyText;
    if (sd.readText(kTempPath, verifyText) != ServiceStatus::Ok)
    {
      io.println(F("Failed to verify /meta.tmp"));
      SD.remove(kTempPath);
      return false;
    }
    JsonDocument verifyDoc;
    const DeserializationError err = deserializeJson(verifyDoc, verifyText);
    if (err)
    {
      io.print(F("Temp parse failed: "));
      io.println(err.c_str());
      SD.remove(kTempPath);
      return false;
    }

    if (SD.exists(kMetaPath) && !SD.remove(kMetaPath))
    {
      io.println(F("Failed to replace existing /meta.json"));
      SD.remove(kTempPath);
      return false;
    }
    if (!SD.rename(kTempPath, kMetaPath))
    {
      io.println(F("Failed to move /meta.tmp to /meta.json"));
      SD.remove(kTempPath);
      return false;
    }

    io.println(F("Saved /meta.json (atomic replace)."));
    io.println(F("Reboot recommended."));
    return true;
  }

  bool MetaConfigEditor::metaGet(const JsonDocument &doc, const String &path, Stream &io)
  {
    io.print(F("[metaGet] path='"));
    io.print(path);
    io.println(F("'"));
    bool ok = false;
    JsonVariantConst v = resolvePathConst(doc, path, &ok);
    io.print(F("[metaGet] resolve ok="));
    io.println(ok ? F("true") : F("false"));
    if (!ok || v.isNull())
    {
      io.println(F("Path not found."));
      return false;
    }
    io.print(F("[metaGet] value type: "));
    if (v.is<JsonObjectConst>())
    {
      io.println(F("object"));
    }
    else if (v.is<JsonArrayConst>())
    {
      io.println(F("array"));
    }
    else if (v.is<const char *>())
    {
      io.println(F("string"));
    }
    else if (v.is<bool>())
    {
      io.println(F("bool"));
    }
    else if (v.is<long>())
    {
      io.println(F("int"));
    }
    else if (v.is<float>())
    {
      io.println(F("float"));
    }
    else
    {
      io.println(F("unknown"));
    }
    serializeJson(v, io);
    io.println();
    return true;
  }

  bool MetaConfigEditor::metaSet(JsonDocument &doc, const String &path, const String &valueText,
                                 Stream &io)
  {
    bool ok = false;
    JsonVariant v = resolvePath(doc, path, true, &ok);
    if (!ok)
    {
      io.println(F("Invalid path."));
      return false;
    }
    const String value = trimCopy(valueText);
    if (value == "true")
    {
      v.set(true);
    }
    else if (value == "false")
    {
      v.set(false);
    }
    else if (value == "null")
    {
      v.clear();
    }
    else if (isIntText(value))
    {
      v.set(value.toInt());
    }
    else if (isFloatText(value))
    {
      v.set(static_cast<float>(value.toFloat()));
    }
    else
    {
      v.set(value);
    }
    io.println(F("OK"));
    return true;
  }

  bool MetaConfigEditor::metaSetJson(JsonDocument &doc, const String &path, const String &jsonText,
                                     Stream &io)
  {
    JsonDocument tmp;
    DeserializationError err = deserializeJson(tmp, jsonText);
    if (err)
    {
      io.print(F("Invalid JSON literal: "));
      io.println(err.c_str());
      return false;
    }

    bool ok = false;
    JsonVariant v = resolvePath(doc, path, true, &ok);
    if (!ok)
    {
      io.println(F("Invalid path."));
      return false;
    }
    v.set(tmp.as<JsonVariantConst>());
    io.println(F("OK"));
    return true;
  }

  bool MetaConfigEditor::metaDel(JsonDocument &doc, const String &path, Stream &io)
  {
    const int dot = path.lastIndexOf('.');
    if (dot < 0)
    {
      JsonObject root = doc.as<JsonObject>();
      if (!root[path.c_str()].isNull())
      {
        root.remove(path.c_str());
        io.println(F("OK"));
        return true;
      }
      io.println(F("Path not found."));
      return false;
    }
    const String parentPath = path.substring(0, dot);
    const String key = path.substring(dot + 1);
    bool ok = false;
    JsonVariant parent = resolvePath(doc, parentPath, false, &ok);
    if (!ok || !parent.is<JsonObject>())
    {
      io.println(F("Parent path not found."));
      return false;
    }
    JsonObject parentObj = parent.as<JsonObject>();
    if (!parentObj[key.c_str()].isNull())
    {
      parentObj.remove(key.c_str());
      io.println(F("OK"));
      return true;
    }
    io.println(F("Path not found."));
    return false;
  }

  bool MetaConfigEditor::fileList(Stream &io)
  {
    indexedFileCount_ = 0;
    hasFreshFileIndex_ = false;

    File root = SD.open("/");
    if (!root)
    {
      io.println(F("Cannot open /"));
      return false;
    }
    if (!root.isDirectory())
    {
      root.close();
      io.println(F("Root is not a directory."));
      return false;
    }

    io.println(F("Files in /:"));
    File entry = root.openNextFile();
    while (entry)
    {
      String name = entry.name();
      if (name.startsWith("/"))
      {
        name = name.substring(1);
      }
      if (name.startsWith("."))
      {
        // Ignore hidden files/directories (macOS metadata, etc.).
      }
      else if (entry.isDirectory())
      {
        // Ignore directories in v1 root file tooling.
      }
      else if (name == "meta.json")
      {
        io.print(F("  [protected] "));
        io.println(name);
      }
      else if (indexedFileCount_ < kMaxIndexedFiles)
      {
        indexedFiles_[indexedFileCount_] = name;
        io.print(F("  "));
        io.print(indexedFileCount_ + 1);
        io.print(F(": "));
        io.println(name);
        ++indexedFileCount_;
      }
      entry.close();
      entry = root.openNextFile();
    }
    root.close();
    hasFreshFileIndex_ = true;
    return true;
  }

  bool MetaConfigEditor::fileCat(SdService &sd, Stream &io, const String &name)
  {
    if (name.length() == 0)
    {
      io.println(F("Usage: file cat <name>"));
      return false;
    }
    String text;
    const String path = String("/") + name;
    if (sd.readText(path.c_str(), text) != ServiceStatus::Ok)
    {
      io.println(F("Read failed."));
      return false;
    }
    io.println(text);
    return true;
  }

  bool MetaConfigEditor::fileRm(SdService &sd, Stream &io, const String &indexText)
  {
    if (!hasFreshFileIndex_)
    {
      io.println(F("Run `file list` first."));
      return false;
    }
    const int idx = indexText.toInt();
    if (idx <= 0 || static_cast<size_t>(idx) > indexedFileCount_)
    {
      io.println(F("Invalid file index."));
      return false;
    }
    const String name = indexedFiles_[idx - 1];
    if (name == "meta.json")
    {
      io.println(F("Deleting meta.json is not allowed."));
      return false;
    }
    const String path = String("/") + name;
    const ServiceStatus status = sd.remove(path.c_str());
    if (status != ServiceStatus::Ok)
    {
      io.println(F("Delete failed."));
      return false;
    }
    io.print(F("Deleted: "));
    io.println(name);
    hasFreshFileIndex_ = false;
    return true;
  }

  bool MetaConfigEditor::fileRmAll(SdService &sd, Stream &io)
  {
    if (!hasFreshFileIndex_)
    {
      io.println(F("Run `file list` first."));
      return false;
    }
    if (indexedFileCount_ == 0)
    {
      io.println(F("No removable files found."));
      hasFreshFileIndex_ = false;
      return true;
    }

    size_t deletedCount = 0;
    size_t failedCount = 0;
    for (size_t i = 0; i < indexedFileCount_; ++i)
    {
      const String &name = indexedFiles_[i];
      if (name.length() == 0 || name == "meta.json")
      {
        continue;
      }
      const String path = String("/") + name;
      if (sd.remove(path.c_str()) == ServiceStatus::Ok)
      {
        ++deletedCount;
        io.print(F("Deleted: "));
        io.println(name);
      }
      else
      {
        ++failedCount;
        io.print(F("Delete failed: "));
        io.println(name);
      }
    }

    io.print(F("file rm all complete. deleted="));
    io.print(deletedCount);
    io.print(F(" failed="));
    io.println(failedCount);
    hasFreshFileIndex_ = false;
    return failedCount == 0;
  }

  JsonVariant MetaConfigEditor::resolvePath(JsonDocument &doc, const String &path, bool createMissing,
                                            bool *ok)
  {
    if (ok)
    {
      *ok = false;
    }
    if (path.length() == 0)
    {
      return JsonVariant();
    }
    JsonVariant current = doc.as<JsonVariant>();
    if (!current.is<JsonObject>())
    {
      if (!createMissing)
      {
        return JsonVariant();
      }
      doc.to<JsonObject>();
      current = doc.as<JsonVariant>();
    }

    int start = 0;
    while (true)
    {
      const int dot = path.indexOf('.', start);
      const String token = dot < 0 ? path.substring(start) : path.substring(start, dot);
      if (token.length() == 0)
      {
        return JsonVariant();
      }

      if (!current.is<JsonObject>())
      {
        return JsonVariant();
      }
      JsonObject obj = current.as<JsonObject>();

      if (dot < 0)
      {
        if (ok)
        {
          *ok = true;
        }
        return obj[token.c_str()];
      }

      if (!obj.containsKey(token.c_str()))
      {
        if (!createMissing)
        {
          return JsonVariant();
        }
        obj[token.c_str()].to<JsonObject>();
      }
      else if (!obj[token.c_str()].is<JsonObject>())
      {
        if (!createMissing)
        {
          return JsonVariant();
        }
        obj.remove(token.c_str());
        obj[token.c_str()].to<JsonObject>();
      }

      current = obj[token.c_str()];
      start = dot + 1;
    }
  }

  JsonVariantConst MetaConfigEditor::resolvePathConst(const JsonDocument &doc, const String &path,
                                                      bool *ok) const
  {
    return resolveMetaDotPath(doc.as<JsonVariantConst>(), path, ok);
  }

  bool MetaConfigEditor::splitTwoArgs(const String &text, String &first, String &rest) const
  {
    const int split = text.indexOf(' ');
    if (split < 0)
    {
      return false;
    }
    first = trimCopy(text.substring(0, split));
    rest = trimCopy(text.substring(split + 1));
    return first.length() > 0 && rest.length() > 0;
  }

  String MetaConfigEditor::trimCopy(const String &text) const
  {
    String s = text;
    s.trim();
    return s;
  }

} // namespace raven
