#include "RTCManager.h"

const char *RTCManager::_daysOfWeek[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

RTCManager::RTCManager() : _pcf8523(nullptr), _ds3231(nullptr), _isInitialized(false), _rtcType(RTCType::UNKNOWN)
{
}

RTCManager::~RTCManager()
{
    delete _pcf8523;
    delete _ds3231;
}

bool RTCManager::begin(RTCType type)
{
    _rtcType = type;
    bool success = false;

    // Clean up any existing instances
    delete _pcf8523;
    delete _ds3231;
    _pcf8523 = nullptr;
    _ds3231 = nullptr;

    switch (type)
    {
    case RTCType::PCF8523:
        _pcf8523 = new RTC_PCF8523();
        success = _pcf8523->begin();
        break;
    case RTCType::DS3231:
        _ds3231 = new RTC_DS3231();
        success = _ds3231->begin();
        break;
    default:
        Serial.println("Unknown RTC type");
        return false;
    }

    if (!success)
    {
        Serial.println("Couldn't find RTC");
        return false;
    }

    if (!_preferences.begin(PREFS_NAMESPACE, false))
    {
        Serial.println("Failed to initialize preferences");
        return false;
    }

    // Only check for new compilation on hard reset
    esp_reset_reason_t reset_reason = esp_reset_reason();
    bool isWakeFromSleep = (reset_reason == ESP_SLEEP_WAKEUP_TIMER);

    if (!isWakeFromSleep && isNewCompilation())
    {
        updateRTC();
        updateCompilationID();
    }
    else if ((type == RTCType::PCF8523 && _pcf8523->lostPower()) ||
             (type == RTCType::DS3231 && _ds3231->lostPower()))
    {
        Serial.println("RTC lost power, updating time from compilation");
        updateRTC();
    }

    _isInitialized = true;
    _preferences.end();
    return true;
}

DateTime RTCManager::now()
{
    if (!_isInitialized)
    {
        return DateTime(DEFAULT_TIMESTAMP);
    }
    switch (_rtcType)
    {
    case RTCType::PCF8523:
        return _pcf8523->now();
    case RTCType::DS3231:
        return _ds3231->now();
    default:
        return DateTime(DEFAULT_TIMESTAMP);
    }
}

void RTCManager::serialPrintDateTime()
{
    DateTime current = now();
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d",
                  current.year(), current.month(), current.day(),
                  current.hour(), current.minute(), current.second());
}

void RTCManager::adjustRTC(uint32_t timestamp)
{
    DateTime dt(timestamp);
    adjustRTC(dt);
}

void RTCManager::adjustRTC(const DateTime &dt)
{
    switch (_rtcType)
    {
    case RTCType::PCF8523:
        if (_pcf8523)
            _pcf8523->adjust(dt);
        break;
    case RTCType::DS3231:
        if (_ds3231)
            _ds3231->adjust(dt);
        break;
    }
}

String RTCManager::getDayOfWeek()
{
    return String(_daysOfWeek[now().dayOfTheWeek()]);
}

uint32_t RTCManager::getUnixTime()
{
    return now().unixtime();
}

DateTime RTCManager::getFutureTime(int days, int hours, int minutes, int seconds)
{
    if (!_isInitialized)
    {
        return DateTime(DEFAULT_TIMESTAMP);
    }
    TimeSpan span(days, hours, minutes, seconds);
    return now() + span;
}

String RTCManager::getCompileDateTime()
{
    // Use compiler macros for build time
    const char *compileDate = __DATE__; // Format: "Mmm dd yyyy"
    const char *compileTime = __TIME__; // Format: "hh:mm:ss"

    // Parse date components
    char month[4];
    int day, year;
    sscanf(compileDate, "%s %d %d", month, &day, &year);

    // Convert month string to number
    int monthNum = 1;
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++)
    {
        if (strncmp(month, months[i], 3) == 0)
        {
            monthNum = i + 1;
            break;
        }
    }

    // Parse time components
    int hour, minute, second;
    sscanf(compileTime, "%d:%d:%d", &hour, &minute, &second);

    char compileDateTime[32];
    // Format with just the compile date and time
    snprintf(compileDateTime, sizeof(compileDateTime),
             "%04d-%02d-%02d %02d:%02d:%02d",
             year, monthNum, day,
             hour, minute, second);

    return String(compileDateTime);
}

DateTime RTCManager::getCompensatedDateTime()
{
    // Parse the compile time directly
    const char *compileDate = __DATE__; // Format: "Mmm dd yyyy"
    const char *compileTime = __TIME__; // Format: "hh:mm:ss"

    // Parse date components
    char month[4];
    int day, year;
    sscanf(compileDate, "%s %d %d", month, &day, &year);

    // Convert month string to number
    int monthNum = 1;
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++)
    {
        if (strncmp(month, months[i], 3) == 0)
        {
            monthNum = i + 1;
            break;
        }
    }

    // Parse time components
    int hour, minute, second;
    sscanf(compileTime, "%d:%d:%d", &hour, &minute, &second);

    // Create DateTime object from compile time
    DateTime compileDateTime(year, monthNum, day, hour, minute, second);

    // Add upload delay compensation
    return compileDateTime + TimeSpan(0, 0, 0, UPLOAD_DELAY_SECONDS);
}

bool RTCManager::isNewCompilation()
{
    _preferences.begin(PREFS_NAMESPACE, false);
    const String currentBuildTime = getCompileDateTime();
    String storedBuildTime = _preferences.getString("buildTime", "");

    // Store new build time
    if (currentBuildTime != storedBuildTime)
    {
        _preferences.putString("buildTime", currentBuildTime);
    }
    _preferences.end();

    Serial.println("\nChecking build status:");
    Serial.println("---------------------------");
    Serial.println("Current build ID:  " + currentBuildTime);
    Serial.println("Previous build ID: " + storedBuildTime);
    Serial.println("Is new upload:     " + String(currentBuildTime != storedBuildTime));
    Serial.println("---------------------------\n");

    return currentBuildTime != storedBuildTime;
}

void RTCManager::updateCompilationID()
{
    _preferences.begin(PREFS_NAMESPACE, false);
    const String currentCompileTime = getCompileDateTime();

    Serial.println("\nUpdating compilation ID:");
    Serial.println("----------------------");
    Serial.println("Storing new compile time: " + currentCompileTime);

    _preferences.putString("compileTime", currentCompileTime.c_str());

    // Verify storage
    String verifyTime = _preferences.getString("compileTime", "");
    Serial.println("Verified stored time:    " + verifyTime);
    Serial.println("Storage successful:      " + String(verifyTime == currentCompileTime));
    Serial.println("----------------------\n");

    _preferences.end();
}

void RTCManager::updateRTC()
{
    Serial.println("\nUpdating RTC time:");
    Serial.println("----------------");
    Serial.println("Compile time: " + getCompileDateTime());

    // Get compensated DateTime
    DateTime compensatedTime = getCompensatedDateTime();

    // Format and print the compensation information
    char timeStr[20];
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d",
             compensatedTime.year(), compensatedTime.month(), compensatedTime.day(),
             compensatedTime.hour(), compensatedTime.minute(), compensatedTime.second());
    Serial.println("Compensated time: " + String(timeStr));

    // Update RTC with compensated time
    adjustRTC(compensatedTime);

    // Verify the time was set correctly
    DateTime currentTime = now();
    char currentTimeStr[20];
    snprintf(currentTimeStr, sizeof(currentTimeStr), "%04d-%02d-%02d %02d:%02d:%02d",
             currentTime.year(), currentTime.month(), currentTime.day(),
             currentTime.hour(), currentTime.minute(), currentTime.second());
    Serial.println("Verified time: " + String(currentTimeStr));
    Serial.println("----------------\n");
}