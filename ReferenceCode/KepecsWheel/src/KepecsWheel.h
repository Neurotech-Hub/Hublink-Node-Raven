#ifndef KEPECS_WHEEL_H
#define KEPECS_WHEEL_H

#include <Arduino.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include "ULPManager.h"
#include "RTCManager.h"
#include "Preferences.h"
#include "Adafruit_MAX1704X.h"
#include "SharedDefs.h"

#define LED_BUILTIN 13 // Built-in LED pin
extern uint8_t SD_CS;  // Make SD_CS accessible to sketches

class KepecsWheel
{
public:
    KepecsWheel(uint8_t wheelType = 2); // Default to DS3231 (type 2)
    bool begin();
    bool logData();
    void sleep(int seconds);
    void adjustRTC(uint32_t timestamp);
    bool shouldSync(int sleepSeconds, int syncMinutes);
    uint32_t getLogCount();
    bool reinit();
    uint8_t getSDCSPin() const { return _sdCSPin; } // Getter for SD_CS pin
    float getBatteryVoltage();
    float getBatteryPercent();

private:
    const char *CSV_HEADER = "datetime,battery_voltage,count";
    String getCurrentFilename();
    bool createFile(String filename);
    void resetLogCount();
    void incrementLogCount();
    void updateSDCSPin();

    RTC_DATA_ATTR static uint32_t _logCount; // Persists in RTC memory
    ULPManager _ulp;
    bool _isWakeFromSleep;
    RTCManager _rtc;
    bool _isRTCInitialized;
    bool _isSDInitialized;
    bool allInitialized;
    bool _beginFailed = false;
    Adafruit_MAX17048 _batteryMonitor;
    RTCType _rtcType;
    uint8_t _sdCSPin = 10; // Default to 10, will be updated based on RTC type
    bool _isBatteryMonitorInitialized;
};

#endif // KEPECS_WHEEL_H