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
- `MetaConfigEditor` for USB Serial `meta.json` and SD file maintenance (used by wheel examples and `MetaConfigEditorHold`)



## Dependencies

Install via Arduino Library Manager (or equivalent) before building examples:

- **Required (library.properties):** RTClib, Adafruit MAX1704X, Adafruit BusIO, Adafruit VEML7700 Library, Adafruit BME680 Library, ArduinoJson
- **Hublink examples only:** [Neurotech-Hub Hublink](https://github.com/Neurotech-Hub/Hublink) — set board **Tools → Bluetooth → NimBLE** for `HublinkBLENode` and `HubWheelHublink`
- **LSM303MagPlotter:** no extra libraries (self-contained I2C helper in the example folder)



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

`#include <HublinkNodeRaven.h>` pulls in `raven::HublinkNode`, sensor/SD services, `DataLoggerHelper`, `LogFileNaming`, `MetaConfigEditor`, `MetaConfigReader`, and low-battery safeguard helpers.

## Examples


| Sketch                                                   | Purpose                                                       |
| -------------------------------------------------------- | ------------------------------------------------------------- |
| `examples/BasicHardware/BasicHardware.ino`               | Minimal bring-up; optional low-battery safeguard demo         |
| `examples/SensorSnapshot/SensorSnapshot.ino`             | One-shot sensor readout over Serial                           |
| `examples/DataLogging/DataLogging.ino`                   | Masked CSV logging to SD with `LogFilePolicy`                 |
| `examples/HubWheelMinimal/HubWheelMinimal.ino`           | Deep-sleep wheel logger (no Hublink)                          |
| `examples/HubWheelHublink/HubWheelHublink.ino`           | Wheel logger + Hublink gateway (NimBLE)                       |
| `examples/HublinkBLENode/HublinkBLENode.ino`             | Hublink gateway + local JX observer logs (NimBLE) — see below |
| `examples/MetaConfigEditorHold/MetaConfigEditorHold.ino` | USB startup hold + `MetaConfigEditor` shell only              |
| `examples/AlertPinTest/AlertPinTest.ino`                 | DS3231 + optional MAX17048, `PIN_ALERT` exercise              |
| `examples/LSM303MagPlotter/LSM303MagPlotter.ino`         | LSM303AGR magnetometer raw XYZ over I2C → Serial Plotter (200 ms, HR mode) |




## Notes

- This library intentionally targets fixed custom hardware. Runtime pin remapping is not supported.
- Defaults are conservative for reliability (`I2C=100kHz`, SD disabled until mounted). Default SD SPI clock is `1 MHz` (`DEFAULT_SD_SPI_CLOCK_HZ` in `RavenPins.h`), aligned with Hublink SD usage when both share the card.
- ULP magnet counting is core hardware functionality; wake cadence and logging policy remain sketch-controlled in `HubWheelMinimal.ino`/`HubWheelHublink.ino`.
- `PIN_ALERT` is a shared hardware interrupt line created by combining `~RTC_INT` and `~FUEL_ALERT` through an AND gate. Because both upstream signals are active-low, if either source asserts LOW, `PIN_ALERT` goes LOW (LOW-level interrupt behavior). This lets sketches monitor one GPIO for either source, but the library does not currently expose APIs to configure specific RTC or fuel-gauge alert thresholds/masks.



## Battery and low-voltage safeguard (voltage-only, millis-based)

- **Defaults:** `[kSafeguardTripVoltsDefault](src/helpers/LowBatteryBoot.h)` (**2.0 V**), `kSafeguardRecoverVoltsDefault` (**2.6 V**), `kSafeguardPollIntervalSecondsDefault` (**600** s), `kSafeguardShutdownWakeupSecondsDefault` (**600** s). No `meta.json` keys.
- **Automatic path:** `[maybeAutomaticVoltageSafeguard(node, true)](src/helpers/LowBatteryBoot.h)`—internal millis spacing and `LowBatteryGateConfig` defaults (**USB** blocks sleep); same behavior as `[DataLoggerHelper::begin()](src/helpers/DataLoggerHelper.cpp)` after `beginI2C()`. Call from `setup()`**/**`loop()` on your cadence—the helper still throttles gauge reads. `maybeAutomaticVoltageSafeguard(node, false)` is a no-op.
- **Manual path:** `[isCellBelowTripVoltage(node[, tripVolts])](src/helpers/LowBatteryBoot.h)`; then optionally `[safeguardShutdown(node, wakeupInSeconds)](src/helpers/LowBatteryBoot.h)` (**LEDs off**, timer deep sleep, does **not** return). USB and policy are sketch-controlled. Example: `[examples/BasicHardware/BasicHardware.ino](examples/BasicHardware/BasicHardware.ino)`.
- **Diagnostics:** `[diagnoseVoltageSafeguard(Stream, node, usbPresent)](src/helpers/LowBatteryBoot.h)`—optional fourth argument `LowBatteryGateConfig` if you tune reporting.
- **Meta editor:** `sensor safeguard` runs diagnose when `HublinkNode` is passed into `maybeEnterWithFade` / `enterNow`.



## Data Logger

- `RtcService::begin()` now performs a best-effort RTC-to-system-time sync when RTC data is valid. If RTC is unavailable or invalid, initialization continues without failing.
- In the API and examples, `SampleFields` means a single combined sensor reading (time + power + light + environment + GPIO states).



### Selectable CSV Fields

- `DataLoggerHelper::csvHeader()` and `DataLoggerHelper::toCsv(...)` keep default full-field behavior for backward compatibility.
- To log only selected columns, use typed masks with `CsvField` and overloads that accept `CsvFieldMask`.
- Battery percentage is exposed as `batt_per` in CSV output.
- `datetime` is formatted as `YYYY-MM-DD HH:MM:SS` for straightforward parsing in Python (`pandas.to_datetime` or `datetime.strptime`). In `meta.json` `logger.log_fields`, the alias `rtc_text` is also accepted for `datetime`.
- `passes_min` is derived from wake cadence: `magnet_passes * 60 / sleep_time_seconds`.
- Full selectable field list:
  - Runtime: `millis`, `ulp_edges`, `magnet_passes`, `passes_min`, `magnet`, `usb_sense`
  - RTC: `unix`, `datetime`, `rtc_temp_c`
  - Battery: `batt_v`, `batt_per`
  - Light: `lux`, `als`, `white`
  - Environment: `temp_c`, `pressure_hpa`, `humidity_pct`, `gas_kohm`, `alt_m`

```cpp
constexpr raven::CsvFieldMask kLogFields = raven::csvFields({
  raven::CsvField::RtcUnix,
  raven::CsvField::UlpEdges,
  raven::CsvField::MagnetPasses,
  raven::CsvField::PassesPerMin,
  raven::CsvField::BattV,
  raven::CsvField::BattPer
});

raven::SampleFields sample = logger.captureSample();
sample.passesPerMin = raven::computePassesPerMinute(sample.magnetPassCount, kSleepTimeSeconds);
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
- `inc_on_reboot` controls whether a 3-digit reboot suffix (`XXX`) is auto-managed by the logger; default is `false`.
- `Daily`:
  - `inc_on_reboot=false`: `[base]_YYYYMMDD.csv` (example: `HUBWHEEL_20260429.csv`)
  - `inc_on_reboot=true`: `[base]_YYYYMMDDXXX.csv` (example: `HUBWHEEL_20260429000.csv`)
- `Hourly`:
  - `inc_on_reboot=false`: `[base]_YYYYMMDDHHMM.csv` (example: `HUBWHEEL_202604291045.csv`)
  - `inc_on_reboot=true`: `[base]_YYYYMMDDHHMMXXX.csv` (example: `HUBWHEEL_202604291045000.csv`)
- `Manual`:
  - `inc_on_reboot=false`: continue writing to the last detected `[base]_XXX.csv` file (or start at `_000` if none exist)
  - `inc_on_reboot=true`: choose the first available `[base]_XXX.csv` on startup
- `Disabled`:
  - `inc_on_reboot=false`: `[base].csv` (example: `HUBWHEEL.csv`)
  - `inc_on_reboot=true`: `[base]XXX.csv` (example: `HUBWHEEL000.csv`)
- If the target file does not exist, the logger writes the CSV header first, then appends rows.
- In `Manual` mode, use `raven::incrementManualCounter(policy)` when you want to advance to the next file explicitly.

```cpp
constexpr char kLogBaseName[] = "LOGGER";
constexpr raven::FileNameMode kLogFileMode = raven::FileNameMode::Disabled;
raven::LogFilePolicy gLogFilePolicy = {
  kLogBaseName,
  kLogFileMode,
  0,     // manualCounter
  false, // manualCounterInitialized
  false  // incOnReboot
};
```



### meta.json programmatic access

- Firmware can read `/meta.json` without going through `Hublink::getMeta` by using Raven helpers (`MetaConfigEditor` already shares the load path internally):
  - `raven::loadMetaJson(sd, doc)` reads and parses a JSON object root.
  - Typed dot-path accessors: `metaGetUInt32`, `metaGetLong`, `metaGetBool`, `metaGetString`, `metaGetJsonArray` (`wheel.sleep_time_seconds`, `logger.log_fields`, etc.).
  - Arbitrary lookups: `raven::resolveMetaDotPath(doc.as<JsonVariantConst>(), "<dot.path>", &ok)`.
- Hublink still owns BLE/upload configuration from `meta.json` via `hublink.begin()`. Sketch-owned namespaces (such as `wheel.*` / `logger.*`) can instead use the Raven APIs so tooling matches the Serial meta editor paths.



### HublinkBLENode (JX observer)

Use `examples/HublinkBLENode/HublinkBLENode.ino` for a **stationary Hublink gateway** that advertises as a JX-family node, syncs with the Hublink cloud, and logs local observer data to SD. Requires the Hublink library and **NimBLE** (not Bluedroid).

**Bring-up (same SD ordering as HubWheelHublink):** `beginHardware` / `beginI2C` → `DataLoggerHelper::begin` → Raven `sd().begin()` → `hublink.begin(advName)` so Hublink sees an already-mounted card.

**Advertising name:** `JX_BBB` + last three hex digits of the Bluetooth MAC (uppercase), e.g. `JX_BBBA1F`.

**Loop (when RTC is valid):** `hublink.sync()` → ~10 s NimBLE active scan → append vitals/settings/BLE rows → delay. Gateway JSON timestamps update the DS3231 via `setTimestampCallback` (same pattern as HubWheelHublink).

**Status LED:** green only — solid during setup after I2C init; one short dip before `hublink.begin`; two flashes before each scan; off when idle.

**Daily SD files** (prefix + `YYYYMMDD`, root path `/JX…csv`). At boot (and each loop when RTC is valid), missing files are created with headers so append paths are ready:


| File               | Role                                | Header / first row                                                                                                                                   |
| ------------------ | ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `/JXVyyyymmdd.csv` | Observer vitals (~60 s)             | Masked columns from `DataLoggerHelper` (`unix`, `datetime`, `batt_v`, `batt_per`, `lux`, `temp_c`, `humidity_pct`, `gas_kohm` in the sketch default) |
| `/JXSyyyymmdd.csv` | Settings snapshot (once per day)    | `fw_version,scan_interval_s,adv_interval_s,vitals_interval,ble_name` then one data row (`adv_interval_s` from `hublink.advertise_every`)             |
| `/JXByyyymmdd.csv` | Nearby JX BLE peers per scan window | `unix,observer_id,peer_id,rssi` — `peer_id` is the advertised **name** (not MAC); only names starting with `JX_`; max RSSI per name per window       |


Sketch constants (`kScanWindowMs`, `kVitalsIntervalMs`, `kFwVersion`, CSV field mask) are defined at the top of `HublinkBLENode.ino`. NimBLE scan runs only from `loop()` with cool-down / forced-deinit guards after Hublink may have torn down the stack. This sketch does **not** use `LogFilePolicy`; filenames are the fixed `JXV` / `JXS` / `JXB` daily prefixes above.

### HubWheel + Hublink Example

- Use `examples/HubWheelHublink/HubWheelHublink.ino` when the Hublink library is installed.
- Use `examples/HubWheelMinimal/HubWheelMinimal.ino` for the Hublink-free wheel logger.
- `HubWheelHublink.ino` keeps hardcoded defaults first, then `hublink.begin()` for `hublink.*`; `wheel.*` / `logger.*` are applied from `/meta.json` using `raven::loadMetaJson` and typed getters.
- The exact `meta.json` example and key details are documented inline in `HubWheelHublink.ino` so the README stays sketch-agnostic.
- Low-power/deep-sleep sketches rely on sketch-controlled wake scheduling; they cannot rely on Hublink advertise/sync intervals while asleep. This is why wheel examples include explicit `wheel.*` timing settings in addition to `hublink.*` settings.
- `HubWheelHublink.ino` includes an optional USB Serial `meta.json` editor: press `e` during a ~5s startup hold window to enter command mode.
- Top-level commands: `help`, `reboot`, `exit`
  - Sensor commands (when `HublinkNode*` is passed into the editor): `sensor`, `sensor list`, `sensor fuel`, `sensor safeguard`, etc.
  - Meta commands: `meta show`, `meta get <path>`, `meta set <path> <value>`, `meta setjson <path> <json_literal>`, `meta del <path>`, `meta save`, `meta reload`
- File commands (root-only): `file help`, `file list`, `file cat <name>`, `file rm <n>`, `file rm all`
  - `file rm` never allows deleting `meta.json`; saves use atomic temp-file replacement (`/meta.tmp` -> `/meta.json`).
- Example session:
  - `meta get wheel.sleep_time_seconds`
  - `meta set wheel.sleep_time_seconds 15`
  - `meta set logger.inc_on_reboot true`
  - `meta setjson logger.log_fields ["unix","datetime","ulp_edges","magnet_passes","passes_min"]`
  - `meta save`
  - `file list`
  - `file rm 2`
  - `file rm all`



### MetaConfigEditorHold

- `examples/MetaConfigEditorHold/MetaConfigEditorHold.ino` isolates the USB startup hold used by the wheel sketches: after cold boot with USB connected, press `e` during the fade window to enter the same `meta` / `file` / `sensor` shell without running wheel or Hublink logic.

