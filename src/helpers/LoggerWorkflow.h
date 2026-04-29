#pragma once

#include "../HublinkNode.h"
#include "DataLoggerHelper.h"
#include "LogFileNaming.h"

namespace raven {

// Generic time-based sync window check used by wake/sleep logging sketches.
bool shouldRunSyncWindow(uint32_t sleepSeconds, uint32_t syncEverySeconds, uint32_t logCount);

// Converts one CSV field name (e.g. "rtc_unix") into a CsvField enum value.
bool parseCsvFieldName(const String &fieldText, CsvField &outField);

// Builds a CsvFieldMask from field names. Unknown names are ignored and can be
// reported to warnOut. Returns fallbackMask if no valid names are found.
CsvFieldMask buildCsvFieldMaskFromNames(const String *fieldNames, size_t count,
                                        CsvFieldMask fallbackMask,
                                        Print *warnOut = nullptr);

// Captures one reading, resolves the configured log path, writes header when needed,
// and appends one CSV row.
ServiceStatus captureAndAppendManagedCsv(DataLoggerHelper &logger, HublinkNode &node,
                                         LogFilePolicy &filePolicy,
                                         CsvFieldMask fieldMask, SampleFields &outSample,
                                         String *outPath = nullptr);

} // namespace raven
