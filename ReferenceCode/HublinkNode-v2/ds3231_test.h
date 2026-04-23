/*
 * DS3231 RTC (I2C). Wire must be started first — see configureBoardPins().
 */

#pragma once

#include "RTClib.h"
#include <Arduino.h>

inline RTC_DS3231 &rtcDs3231() {
  static RTC_DS3231 rtc;
  return rtc;
}

static const char *const kRtcDaysOfWeek[] = {
    "Sunday",   "Monday", "Tuesday",  "Wednesday",
    "Thursday", "Friday", "Saturday",
};

/** Single attempt: init + lostPower handling + one read, or skip. */
inline void ds3231TestOnce() {
  Serial.println(F("\n--- DS3231 ---"));
  if (!rtcDs3231().begin(&Wire)) {
    Serial.println(F("DS3231: not found (skip)."));
    return;
  }

  if (rtcDs3231().lostPower()) {
    Serial.println(F("DS3231: lost power — set from compile time."));
    rtcDs3231().adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime now = rtcDs3231().now();
  if (!now.isValid()) {
    Serial.println(F("DS3231: invalid time read (I2C error, skip)."));
    return;
  }
  uint8_t day = now.dayOfTheWeek();
  const char *dayName = (day < 7) ? kRtcDaysOfWeek[day] : "Unknown";

  Serial.print(F("RTC: "));
  Serial.print(now.year(), DEC);
  Serial.print('/');
  Serial.print(now.month(), DEC);
  Serial.print('/');
  Serial.print(now.day(), DEC);
  Serial.print(F(" ("));
  Serial.print(dayName);
  Serial.print(F(") "));
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.print(now.second(), DEC);
  Serial.print(F("  unix="));
  Serial.print(now.unixtime());
  Serial.print(F("  temp="));
  Serial.print(rtcDs3231().getTemperature());
  Serial.println(F(" C"));
}
