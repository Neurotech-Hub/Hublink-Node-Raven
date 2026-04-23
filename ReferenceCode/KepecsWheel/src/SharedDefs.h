#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

// Preferences namespace (shared across library)
#define PREFS_NAMESPACE "kepecs"

// Time constants
#define SECONDS_FROM_1970_TO_2000 946684800

// RTC type definition
enum class RTCType
{
    PCF8523,
    DS3231,
    UNKNOWN
};

#endif