#include "RtcWallTime.h"

namespace raven {
namespace {

int readDigits(const char *text, uint8_t count) {
  int value = 0;
  for (uint8_t i = 0; i < count; ++i) {
    value = value * 10 + (text[i] - '0');
  }
  return value;
}

bool isDigitPair(const char *text) {
  return text[0] >= '0' && text[0] <= '9' && text[1] >= '0' && text[1] <= '9';
}

bool isLeapYear(int year) {
  return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

bool isValidCivilDay(int year, int month, int day) {
  static const uint8_t kDaysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12 || day < 1) {
    return false;
  }
  uint8_t days = kDaysInMonth[month];
  if (month == 2 && isLeapYear(year)) {
    days = 29;
  }
  return day <= days;
}

} // namespace

bool parseWallDateTime(const char *text, DateTime &out) {
  if (text == nullptr) {
    return false;
  }
  for (uint8_t i = 0; i < 19; ++i) {
    if (text[i] == '\0') {
      return false;
    }
  }
  if (text[19] != '\0') {
    return false;
  }
  if (text[4] != '-' || text[7] != '-' || text[10] != ' ' || text[13] != ':' || text[16] != ':') {
    return false;
  }
  for (uint8_t i = 0; i < 19; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) {
      continue;
    }
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  const int year = readDigits(text, 4);
  const int month = readDigits(text + 5, 2);
  const int day = readDigits(text + 8, 2);
  const int hour = readDigits(text + 11, 2);
  const int minute = readDigits(text + 14, 2);
  const int second = readDigits(text + 17, 2);
  if (year < 2000 || year > 2099 || !isValidCivilDay(year, month, day) || hour > 23 ||
      minute > 59 || second > 59) {
    return false;
  }
  const DateTime parsed(static_cast<uint16_t>(year), static_cast<uint8_t>(month),
                        static_cast<uint8_t>(day), static_cast<uint8_t>(hour),
                        static_cast<uint8_t>(minute), static_cast<uint8_t>(second));
  if (!parsed.isValid()) {
    return false;
  }
  out = parsed;
  return true;
}

bool isTimezoneOffsetLabel(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  if ((text[0] == 'Z' || text[0] == 'z') && text[1] == '\0') {
    return true;
  }
  if (text[0] != '+' && text[0] != '-') {
    return false;
  }
  for (uint8_t i = 1; i < 6; ++i) {
    if (text[i] == '\0') {
      return false;
    }
  }
  if (text[6] != '\0' || text[3] != ':') {
    return false;
  }
  if (!isDigitPair(text + 1) || !isDigitPair(text + 4)) {
    return false;
  }
  const int hours = readDigits(text + 1, 2);
  const int minutes = readDigits(text + 4, 2);
  return hours <= 14 && minutes <= 59;
}

} // namespace raven
