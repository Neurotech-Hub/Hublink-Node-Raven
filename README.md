# Hublink Node Raven

`Hublink-Node-Raven` is a hardware interface library for the fixed Hublink Node v2 board.
It exposes low-level board controls plus optional higher-level helpers for monitoring and data logging.

## Features

- Fixed hardware pin map and rail controls for Hublink Node v2
- Service wrappers for:
  - DS3231 RTC
  - MAX17048 battery gauge
  - VEML7700 ambient light
  - BME680 environmental sensing
  - SD card storage (SPI)
- Optional data logging helper that generates CSV rows from a composite sample
- Built-in ULP magnet edge counting service for deep-sleep wheel applications

## Arduino IDE Setup

- In Arduino IDE, select board `ESP32S3 Dev Module` from the ESP32 board package.
- In Tools, set `USB CDC On Boot` to `Enabled`.
- On Raven hardware, it is recommended to enter boot mode before flashing:
  - hold the `Boot` button
  - briefly press `Reset`
  - release `Boot`
- After flashing completes, press `Reset` once to start the new firmware.

## Quick start

```cpp
#include <HublinkNodeRaven.h>

raven::HublinkNode node;

void setup() {
  Serial.begin(115200);
  node.beginHardware();
  node.beginI2C();
  node.rtc().begin();
}

void loop() {
  raven::RtcReading rtc = node.rtc().readSample();
  if (rtc.status == raven::ServiceStatus::Ok) {
    Serial.println(rtc.now.unixtime());
  }
  delay(1000);
}
```

## Examples

- `examples/BasicHardware/BasicHardware.ino`
- `examples/SensorSnapshot/SensorSnapshot.ino`
- `examples/DataLogging/DataLogging.ino`
- `examples/HubWheel/HubWheel.ino`
- `examples/AlertPinTest/AlertPinTest.ino` (DS3231 + optional MAX17048, `PIN_ALERT` exercise)

## Notes

- This library intentionally targets fixed custom hardware. Runtime pin remapping is not supported.
- Defaults are conservative for reliability (`I2C=100kHz`, SD disabled until mounted).
- ULP magnet counting is core hardware functionality; wake cadence and logging policy remain sketch-controlled in `HubWheel.ino`.
- `PIN_ALERT` is a shared hardware interrupt line created by combining `~RTC_INT` and `~FUEL_ALERT` through an AND gate. Because both upstream signals are active-low, if either source asserts LOW, `PIN_ALERT` goes LOW (LOW-level interrupt behavior). This lets sketches monitor one GPIO for either source, but the library does not currently expose APIs to configure specific RTC or fuel-gauge alert thresholds/masks.

## Hardware Power Profile

- Measured low-power modes on Raven:
  - Deep sleep baseline: `50 uA` (`0.05 mA`)
  - `examples/HubWheel/HubWheel.ino` (ULP enabled): `236 uA` (`0.236 mA`)
- Estimated battery life in deep sleep baseline (`50 uA`):
  - `100 mAh`: ~`2000` hours (~`83` days)
  - `500 mAh`: ~`10000` hours (~`417` days)
  - `1000 mAh`: ~`20000` hours (~`833` days)
  - `2000 mAh`: ~`40000` hours (~`1667` days)
- Estimated battery life in HubWheel ULP mode (`236 uA`):
  - `100 mAh`: ~`424` hours (~`18` days)
  - `500 mAh`: ~`2119` hours (~`88` days)
  - `1000 mAh`: ~`4237` hours (~`177` days)
  - `2000 mAh`: ~`8475` hours (~`353` days)
- Active current is workload-dependent and typically ranges from about `20-50 mA` depending on clock speed and sensor/SD card utilization.
