#include <KepecsWheel.h>
#include <Hublink.h>

// Initialize KepecsWheel with wheel type 2 (DS3231)
// Use wheel type 1 for PCF8523
KepecsWheel wheel; // Default constructor uses type 2 (DS3231)
Hublink hublink(SD_CS);

int SLEEP_TIME_SECONDS = 10;
int SYNC_EVERY_MINUTES = 360; // 4 hours
int SYNC_FOR_SECONDS = 30;

void onTimestampReceived(uint32_t timestamp)
{
  Serial.println("Received timestamp: " + String(timestamp));
  wheel.adjustRTC(timestamp);
}

void setup()
{
  Serial.begin(115200);
  Serial.println("KepecsWheel Basic Example");

  if (!wheel.begin())
  {
    Serial.println("Failed to initialize KepecsWheel!");
    while (1)
      delay(10);
  }

  Serial.println("KepecsWheel initialized successfully!");

  // log and increment log count
  wheel.logData();

  // loads vars from meta.json on hard reset
  if (wheel.reinit())
  {
    beginHublink();
  }

  // uses logCount to determine if it should sync
  if (wheel.shouldSync(SLEEP_TIME_SECONDS, SYNC_EVERY_MINUTES))
  {
    hublink.setBatteryLevel((int)wheel.getBatteryPercent());
    hublink.sync(SYNC_FOR_SECONDS); // force sync
  }

  // deep sleep
  wheel.sleep(SLEEP_TIME_SECONDS);
}

void loop()
{
  if (wheel.logData())
  {
    Serial.println("Data logged successfully");
  }
  else
  {
    Serial.println("Failed to log data");
  }

  // Sleep for 60 seconds
  wheel.sleep(60);
}

void beginHublink()
{
  if (hublink.begin())
  {
    Serial.println("✓ Hublink.");
    hublink.setTimestampCallback(onTimestampReceived);

    // override default values with values from meta.json
    if (hublink.hasMetaKey("wheel", "sleep_time_seconds"))
    {
      SLEEP_TIME_SECONDS = hublink.getMeta<int>("wheel", "sleep_time_seconds");
      Serial.println("SLEEP_TIME_SECONDS: " + String(SLEEP_TIME_SECONDS));
    }
    if (hublink.hasMetaKey("wheel", "sync_every_minutes"))
    {
      SYNC_EVERY_MINUTES = hublink.getMeta<int>("wheel", "sync_every_minutes");
      Serial.println("SYNC_EVERY_MINUTES: " + String(SYNC_EVERY_MINUTES));
    }
    if (hublink.hasMetaKey("wheel", "sync_for_seconds"))
    {
      SYNC_FOR_SECONDS = hublink.getMeta<int>("wheel", "sync_for_seconds");
      Serial.println("SYNC_FOR_SECONDS: " + String(SYNC_FOR_SECONDS));
    }
  }
  else
  {
    Serial.println("✗ Hublink Failed.");
  }
}