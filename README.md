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
- Optional data logging helper that generates CSV rows from a combined sensor reading
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

## Data Logger

- `RtcService::begin()` now performs a best-effort RTC-to-system-time sync when RTC data is valid. If RTC is unavailable or invalid, initialization continues without failing.
- In the API and examples, `SampleFields` means a single combined sensor reading (time + power + light + environment + GPIO states).

### Selectable CSV Fields

- `DataLoggerHelper::csvHeader()` and `DataLoggerHelper::toCsv(...)` keep default full-field behavior for backward compatibility.
- To log only selected columns, use typed masks with `CsvField` and overloads that accept `CsvFieldMask`.
- Battery percentage is exposed as `batt_per` in CSV output.
- Full selectable field list:
  - Runtime: `millis`, `ulp_edges`, `magnet_passes`, `magnet`, `usb_sense`
  - RTC: `rtc_unix`, `rtc_temp_c`
  - Battery: `batt_v`, `batt_per`
  - Light: `lux`, `als`, `white`
  - Environment: `temp_c`, `pressure_hpa`, `humidity_pct`, `gas_kohm`, `alt_m`

```cpp
constexpr raven::CsvFieldMask kLogFields = raven::csvFields({
  raven::CsvField::RtcUnix,
  raven::CsvField::UlpEdges,
  raven::CsvField::MagnetPasses,
  raven::CsvField::BattV,
  raven::CsvField::BattPer
});

raven::SampleFields sample = logger.captureSample();
String logPath;
if (raven::resolveLogFilePath(node.sd(), gLogFilePolicy, sample.rtc, logPath) ==
    raven::ServiceStatus::Ok) {
  if (!node.sd().exists(logPath.c_str())) {
    node.sd().appendLine(logPath.c_str(), raven::DataLoggerHelper::csvHeader(kLogFields));
  }
  logger.appendCsvSample(logPath.c_str(), sample, kLogFields);
}
```

### Filename Modes

- Base name is required and should use only letters, numbers, `_`, or `-`.
- `Daily`: `[base]_YYYYMMDD.csv` (example: `HUBWHEEL_20260429.csv`)
- `Hourly`: `[base]_YYYYMMDDHHMM.csv` (example: `HUBWHEEL_202604291045.csv`)
- `Manual`: `[base]_XXXXX.csv` (example: `HUBWHEEL_00000.csv`)
- `Disabled`: `[base].csv` (example: `HUBWHEEL.csv`)
- If the target file does not exist, the logger writes the CSV header first, then appends rows.
- Manual mode initializes by scanning from `_00000` upward and selecting the first available index. Use `raven::incrementManualCounter(policy)` when you want to advance to the next manual file.

```cpp
constexpr char kLogBaseName[] = "LOGGER";
constexpr raven::FileNameMode kLogFileMode = raven::FileNameMode::Disabled;
raven::LogFilePolicy gLogFilePolicy = {kLogBaseName, kLogFileMode, 0, false};
```

## Hardware Power Profile

- Measured low-power modes on Raven:
  - Deep sleep baseline: `50 uA` (`0.05 mA`)
  - `examples/HubWheel/HubWheel.ino` (ULP enabled): `236 uA` (`0.236 mA`)
- Estimated battery life:

| Battery | Deep sleep baseline (`50 uA`) | HubWheel ULP mode (`236 uA`) |
| --- | --- | --- |
| `100 mAh` | ~`2000` hours (~`83` days) | ~`424` hours (~`18` days) |
| `500 mAh` | ~`10000` hours (~`417` days) | ~`2119` hours (~`88` days) |
| `1000 mAh` | ~`20000` hours (~`833` days) | ~`4237` hours (~`177` days) |
| `2000 mAh` | ~`40000` hours (~`1667` days) | ~`8475` hours (~`353` days) |
- Magnet sensor (Allegro Microsystems `APS11753KMDALX-3PL1`): about `56 uA` with a `1.5 ms` sampling period—a reasonable tradeoff between higher-frequency magnetic sensing and low power. The part may be removed or replaced with other common magnet sensors (including latching types); higher bandwidth and latching types often draw upwards of `1 mA` constantly.
- Active current is workload-dependent and typically ranges from about `20-50 mA` depending on clock speed and sensor/SD card utilization.
