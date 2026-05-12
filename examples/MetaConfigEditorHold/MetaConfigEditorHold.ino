#include <HublinkNodeRaven.h>
#include <esp_sleep.h>

// MetaConfigEditorHold — minimal example of the USB startup hold used by HubWheelHublink /
// HubWheelMinimal: after reset, if USB is present, the sketch spends ~3s in maybeEnterWithFade
// where you can press 'e' to open the meta / file / sensor shell. Otherwise it returns immediately.

raven::HublinkNode node;
raven::MetaConfigEditor metaEditor;

void setup()
{
  Serial.begin(115200);
  pinMode(raven::PIN_LED_GREEN, OUTPUT);
  digitalWrite(raven::PIN_LED_GREEN, LOW);
  digitalWrite(raven::PIN_LED_GREEN, HIGH);

  node.beginHardware();
  node.beginI2C();
  node.rtc().begin();
  node.powerGauge().begin();
  node.light().begin();
  node.environment().begin();

  if (!node.sd().begin() || node.sd().cardType() == CARD_NONE)
  {
    Serial.println(F("MetaConfigEditorHold: SD card required. Halting."));
    while (true)
    {
      delay(1000);
    }
  }

  const esp_sleep_wakeup_cause_t cause = node.wakeupCause();
  // Same gate as wheel examples: only after cold boot / reset, not after timer deep sleep wake.
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && node.readUsbSense())
  {
    metaEditor.maybeEnterWithFade(node.sd(), true, Serial, 3000,
                                  raven::PIN_LED_GREEN, raven::PIN_LED_BLUE, &node);
  }

  Serial.println(F("MetaConfigEditorHold: hold finished; loop idle."));
}

void loop()
{
  delay(1000);
}
