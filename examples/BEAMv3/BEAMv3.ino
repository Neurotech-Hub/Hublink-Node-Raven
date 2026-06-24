// BEAMv3 — ULP motion counter on PIN_AUX_GPIO1 during deep sleep.
//
// Each wake: print motion_count from the previous sleep window (timer wake only), re-arm the ULP
// program, deep sleep for kSleepSeconds. loop() is empty; setup() never returns.

#include <HublinkNodeRaven.h>
#include <esp_sleep.h>

static constexpr uint32_t kSleepSeconds = 10;
static constexpr uint32_t kUsbSerialSettleMs = 2000;

raven::HublinkNode node;

static void enterDeepSleep()
{
  node.motionCounter().clearCount();
  node.motionCounter().begin(static_cast<gpio_num_t>(raven::PIN_AUX_GPIO1));
  if (!node.motionCounter().start())
  {
    Serial.println(F("BEAMv3: ULP start failed; halting."));
    while (true)
    {
      delay(1000);
    }
  }

  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(kSleepSeconds) * 1000000ULL);
  Serial.printf("BEAMv3: entering deep sleep for %lus\n",
                static_cast<unsigned long>(kSleepSeconds));
  Serial.flush();
  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  node.beginHardware();
  if (node.readUsbSense())
  {
    delay(kUsbSerialSettleMs);
  }

  const esp_sleep_wakeup_cause_t cause = node.wakeupCause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER)
  {
    Serial.printf("motion_count=%u\n", node.motionCounter().motionCount());
  }
  else
  {
    Serial.println(F("BEAMv3: power-on — ULP motion test on PIN_AUX_GPIO1"));
  }

  enterDeepSleep();
}

void loop()
{
  // setup() never returns (esp_deep_sleep_start). loop() is intentionally empty.
}
