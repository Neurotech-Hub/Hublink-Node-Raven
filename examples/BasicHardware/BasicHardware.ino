#include <HublinkNodeRaven.h>

raven::HublinkNode node;

void setup() {
  Serial.begin(115200);
  node.beginHardware();
  node.beginI2C();
}

void loop() {
  const bool magnetHigh = node.readMagnet();
  node.setStatusLeds(!magnetHigh);

  Serial.print(F("MAG_OUT="));
  Serial.print(magnetHigh ? F("HIGH") : F("LOW"));
  Serial.print(F(" USB_SENSE="));
  Serial.println(node.readUsbSense() ? F("HIGH") : F("LOW"));
  delay(1000);
}
