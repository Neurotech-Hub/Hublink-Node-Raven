#include <stdio.h>
#include "KepecsWheel.h"

// Initialize static members
RTC_DATA_ATTR uint32_t KepecsWheel::_logCount = 0;
uint8_t SD_CS = 10; // Default to 10, will be updated based on RTC type

// I2C address for RTCs
#define RTC_ADDRESS 0x68

// DS3231 temperature register
#define DS3231_TEMP_REG 0x11

KepecsWheel::KepecsWheel(uint8_t wheelType)
{
    // Set RTC type based on wheel type
    _rtcType = (wheelType == 2) ? RTCType::DS3231 : RTCType::PCF8523;
}

void KepecsWheel::updateSDCSPin()
{
    if (_rtcType == RTCType::DS3231)
    {
        _sdCSPin = A0; // DS3231 board uses A0 for SD_CS
        SD_CS = A0;    // Update global variable
    }
    else
    {
        _sdCSPin = 10; // PCF8523 board uses pin 10
        SD_CS = 10;    // Update global variable
    }
}

bool KepecsWheel::begin()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    _isWakeFromSleep = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);
    Serial.printf("Wakeup reason: %d\n", wakeup_reason);

    // Reset counter on hard reset, increment on timer wakeup
    if (!_isWakeFromSleep)
    {
        resetLogCount();
    }

    Serial.printf("Log count: %d\n", _logCount);
    pinMode(LED_BUILTIN, OUTPUT);

    Wire.begin();
    delay(10); // Give I2C time to stabilize

    // Update SD_CS pin based on RTC type
    updateSDCSPin();

    // Now initialize SD with the correct CS pin
    SPI.begin(SCK, MISO, MOSI, _sdCSPin);
    if (SD.begin(_sdCSPin, SPI, 1000000))
    {
        Serial.println("SD Card initialized.");
        _isSDInitialized = true;
    }
    else
    {
        Serial.println("SD Card initialization failed.");
        _isSDInitialized = false;
    }

    // Initialize RTC based on type
    Serial.printf("  RTC: Initializing %s\n", (_rtcType == RTCType::DS3231) ? "DS3231" : "PCF8523");
    _isRTCInitialized = _rtc.begin(_rtcType);

    // Set appropriate ULP sensor pin based on RTC type
    if (_rtcType == RTCType::DS3231)
    {
        _ulp.setSensorPin(GPIO_NUM_16); // v2 DS3231 board uses GPIO16/A2
    }
    else
    {
        _ulp.setSensorPin(GPIO_NUM_18); // v1 PCF8523 board uses GPIO18
    }

    // Initialize battery monitor with detailed debug
    if (!_batteryMonitor.begin(&Wire))
    {
        Serial.println("  Battery: failed to begin()");
        _isBatteryMonitorInitialized = false;
    }
    else
    {
        Serial.println("  Battery: begin() OK");
        _isBatteryMonitorInitialized = true;

        // Replace single delay with retry loop
        const uint8_t MAX_RETRIES = 10;
        uint8_t retries = 0;
        float voltage = 0;

        while (retries < MAX_RETRIES)
        {
            delay(1); // Shorter delay between attempts
            voltage = _batteryMonitor.cellVoltage();
            Serial.printf("  Battery init - attempt %d: %.2fV\n", retries + 1, voltage);

            if (voltage > 0 && !isnan(voltage))
            {
                break; // Valid reading obtained
            }
            retries++;
        }
    }

    allInitialized = _isSDInitialized && _isRTCInitialized && _isBatteryMonitorInitialized;
    if (!allInitialized)
    {
        _beginFailed = true;
        Serial.print("Failure reason: ");
        if (!_isSDInitialized)
            Serial.println("SD Card");
        if (!_isRTCInitialized)
            Serial.println("RTC");
        if (!_isBatteryMonitorInitialized)
            Serial.println("Battery Monitor");
    }
    return allInitialized;
}

bool KepecsWheel::reinit()
{
    return _isWakeFromSleep || _beginFailed;
}

bool KepecsWheel::logData()
{
    // Only log data if we woke from sleep
    if (!_isWakeFromSleep)
    {
        Serial.println("Not waking from sleep, skipping data logging");
        return false;
    }
    digitalWrite(LED_BUILTIN, HIGH);

    String currentFile = getCurrentFilename();

    // Check if file exists, create it with header if it doesn't
    if (!SD.exists(currentFile))
    {
        if (!createFile(currentFile))
        {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(1000); // linger for a moment on error
            return false;
        }
    }

    // Open file in append mode
    File dataFile = SD.open(currentFile, FILE_APPEND);
    if (!dataFile)
    {
        Serial.println("Failed to open file for logging: " + currentFile);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(1000); // linger for a moment on error
        return false;
    }

    DateTime now = _rtc.now();

    char datetime[20];
    snprintf(datetime, sizeof(datetime), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());

    char voltageStr[8];
    snprintf(voltageStr, sizeof(voltageStr), "%.2f", getBatteryVoltage());

    String dataString = "";
    dataString = String(datetime) + "," + String(voltageStr) + "," + String((int)(_ulp.getEdgeCount() / 2));

    Serial.printf("\nLogging data: %s\n\n", dataString.c_str());

    // Write data
    bool success = dataFile.println(dataString);
    dataFile.close();
    incrementLogCount();
    digitalWrite(LED_BUILTIN, LOW);
    return success;
}

void KepecsWheel::sleep(int seconds)
{
    digitalWrite(LED_BUILTIN, LOW);
    if (_isBatteryMonitorInitialized)
    {
        _batteryMonitor.enableSleep(true); // Enable sleep capability
        _batteryMonitor.sleep(true);       // Enter sleep mode
    }
    uint64_t microseconds = (uint64_t)seconds * 1000000ULL;
    esp_sleep_enable_timer_wakeup(microseconds);
    _ulp.clearEdgeCount();
    _ulp.begin();
    _ulp.start();
    esp_deep_sleep_start();
}

String KepecsWheel::getCurrentFilename()
{
    DateTime now = _rtc.now();
    char filename[20];
    snprintf(filename, sizeof(filename), "/WHEEL_%04d%02d%02d.csv",
             now.year(), now.month(), now.day());
    return String(filename);
}

bool KepecsWheel::createFile(String filename)
{
    // Ensure filename starts with a forward slash
    if (!filename.startsWith("/"))
    {
        filename = "/" + filename;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file)
    {
        Serial.println("Failed to create file: " + filename);
        return false;
    }

    // Write header row
    if (file.println(CSV_HEADER))
    {
        Serial.println("Created new log file: " + filename);
        file.close();
        return true;
    }

    Serial.println("Failed to write header to file: " + filename);
    file.close();
    return false;
}

void KepecsWheel::adjustRTC(uint32_t timestamp)
{
    _rtc.adjustRTC(timestamp);
}

void KepecsWheel::resetLogCount()
{
    _logCount = 0;
}

void KepecsWheel::incrementLogCount()
{
    _logCount++;
}

uint32_t KepecsWheel::getLogCount()
{
    return _logCount;
}

bool KepecsWheel::shouldSync(int sleepSeconds, int syncMinutes)
{
    // Convert everything to minutes for comparison
    float elapsedMinutes = (float)(sleepSeconds * _logCount) / 60.0;
    Serial.printf("Sync check: elapsed=%.2f minutes, threshold=%d minutes\n", elapsedMinutes, syncMinutes);
    bool shouldSync = elapsedMinutes >= syncMinutes;
    if (shouldSync)
    {
        resetLogCount();
    }
    return shouldSync;
}

float KepecsWheel::getBatteryVoltage()
{
    return _batteryMonitor.cellVoltage();
}

float KepecsWheel::getBatteryPercent()
{
    return _batteryMonitor.cellPercent();
}