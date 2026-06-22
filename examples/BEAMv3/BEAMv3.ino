// BEAMv3 — minimal Raven bring-up and fast poll of PIN_AUX_GPIO1.
//
// beginHardware() configures the aux GPIO as INPUT_PULLUP by default; this sketch
// reconfigures PIN_AUX_GPIO1 as INPUT_PULLDOWN before polling.
// loop() reads continuously and prints only when the pin level changes.

#include <HublinkNodeRaven.h>

raven::HublinkNode node;

static int gLastGpio1 = -1;

void setup()
{
  Serial.begin(115200);
  delay(1000);
// beginHardware() configures aux GPIOs as INPUT_PULLUP; we override GPIO1 to pulldown.
  node.beginHardware();
  pinMode(raven::PIN_AUX_GPIO1, INPUT_PULLDOWN);
  Serial.print(F("MCU clock MHz: "));
  Serial.println(raven::HublinkNode::mcuClockMhz());
  // Match BasicHardware: enable the aux I2C rail and bring up Wire.
  node.setI2CPowerEnabled(true);
  node.beginI2C();
  gLastGpio1 = digitalRead(raven::PIN_AUX_GPIO1);
  Serial.println(F("BEAMv3: PIN_AUX_GPIO1 — print on change only"));
}

void loop()
{
  const int gpio1 = digitalRead(raven::PIN_AUX_GPIO1);
  if (gpio1 != gLastGpio1)
  {
    gLastGpio1 = gpio1;
    Serial.printf("GPIO1=%d\n", gpio1);
  }
}
