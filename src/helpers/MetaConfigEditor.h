#pragma once

#include "../services/SdService.h"
#include "../hardware/HublinkPins.h"
#include <Arduino.h>
#include <ArduinoJson.h>

namespace raven {

class HublinkNode;

class MetaConfigEditor {
public:
  bool maybeEnter(SdService &sd, bool usbPresent, Stream &io = Serial,
                  uint32_t holdWindowMs = 3000, HublinkNode *node = nullptr);
  bool maybeEnterWithFade(SdService &sd, bool usbPresent, Stream &io = Serial,
                          uint32_t holdWindowMs = 3000,
                          uint8_t ledPrimaryPin = PIN_LED_GREEN,
                          uint8_t ledSecondaryPin = PIN_LED_B,
                          HublinkNode *node = nullptr);
  bool enterNow(SdService &sd, Stream &io = Serial, HublinkNode *node = nullptr);

private:
  static constexpr const char *kMetaPath = "/meta.json";
  static constexpr const char *kTempPath = "/meta.tmp";
  static constexpr size_t kMaxInputLine = 256;
  static constexpr size_t kMaxIndexedFiles = 64;

  String indexedFiles_[kMaxIndexedFiles];
  size_t indexedFileCount_ = 0;
  bool hasFreshFileIndex_ = false;

  bool runShell(SdService &sd, Stream &io, HublinkNode *node);
  bool readLine(Stream &io, String &lineOut);
  bool handleCommand(SdService &sd, Stream &io, const String &line, bool &shouldExit);

  void printTopHelp(Stream &io) const;
  void printMetaHelp(Stream &io) const;
  void printFileHelp(Stream &io) const;
  void printSensorHelp(Stream &io) const;

  bool cmdSensor(Stream &io, HublinkNode *node, const String &rest);

  bool cmdMeta(Stream &io, SdService &sd, const String &rest);
  bool cmdFile(Stream &io, SdService &sd, const String &rest);

  bool loadMetaDoc(SdService &sd, JsonDocument &doc, Stream &io);
  bool saveMetaDocAtomic(SdService &sd, const JsonDocument &doc, Stream &io);

  bool metaGet(const JsonDocument &doc, const String &path, Stream &io);
  bool metaSet(JsonDocument &doc, const String &path, const String &valueText, Stream &io);
  bool metaSetJson(JsonDocument &doc, const String &path, const String &jsonText, Stream &io);
  bool metaDel(JsonDocument &doc, const String &path, Stream &io);

  bool fileList(Stream &io);
  bool fileCat(SdService &sd, Stream &io, const String &name);
  bool fileRm(SdService &sd, Stream &io, const String &indexText);
  bool fileRmAll(SdService &sd, Stream &io);

  JsonVariant resolvePath(JsonDocument &doc, const String &path, bool createMissing,
                          bool *ok = nullptr);
  JsonVariantConst resolvePathConst(const JsonDocument &doc, const String &path,
                                    bool *ok = nullptr) const;
  bool splitTwoArgs(const String &text, String &first, String &rest) const;
  String trimCopy(const String &text) const;
};

} // namespace raven
