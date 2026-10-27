#pragma once

#include <RTClib.h>

namespace raven {

// `text` must be exactly `YYYY-MM-DD HH:MM:SS`. The DS3231 stores these fields as given.
bool parseWallDateTime(const char *text, DateTime &out);

// Label only: `Z`, `z`, or `±HH:MM` with hours 00–14. Not applied to the clock.
bool isTimezoneOffsetLabel(const char *text);

} // namespace raven
