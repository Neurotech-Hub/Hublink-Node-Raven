# KepecsWheel Library

An ESP32-based Arduino library for monitoring and logging mouse wheel rotations in neuroscience experiments.

## Wheel Types

The library supports two types of KepecsWheel boards:

1. **PCF8523 (Type 1)**
   - Uses PCF8523 RTC
   - SD card CS pin: 10
   - ULP sensor pin: GPIO18
   - Default for older boards

2. **DS3231 (Type 2)**
   - Uses DS3231 RTC
   - SD card CS pin: A0
   - ULP sensor pin: GPIO16/A2
   - Default for newer boards

To specify the wheel type in your sketch:
```cpp
// For DS3231 (newer) boards (default)
KepecsWheel wheel();

// For PCF8523 (older) boards
KepecsWheel wheel(1);
```

## Updating Firmware

1. Download the KepecsWheel library for the Arduino IDE or manually clone/download the repository from [Neurotech-Hub/KepecsWheel](https://github.com/Neurotech-Hub/KepecsWheel). For downloaded libraries, go to Sketch -> Include Library -> Add .ZIP Library, or place the library in the `libraries` folder in the Arduino IDE.
2. If the device has been previously flashed, it may be in a sleep state that prevents it from connecting to the serial port. To enter boot mode, hold the `Boot` button and toggle the `Reset` button (then release the `Boot` button).
3. If the Arduino IDE does not indicate that it is connected to "Adafruit Feather ESP32-S3 2MB PSRAM", click Tools -> Board -> esp32 -> Adafruit Feather ESP32-S3 2MB PSRAM (you will need to download the esp32 board package (by espressif) from the Arduino IDE).

If you wish to modify the sketch itself, it is highly recommended to become a collaborator on this repository and use proper git workflows. This will reduce the risk of delpoying out of date code and makes identifying errors easier across multilpe authors.

### RTC Syncing

The initial RTC setting is done by checking the compile time of the sketch. This may require clearing the Arduino cache before compiling. For example, on MacOS, this can be done by running `sudo rm -rf ~/Library/Caches/arduino/sketches` (or removing it manually). See below for details on Hublink RTC syncing.

## Data Format

The data is logged in a CSV file with the following format:

```
datetime,battery_voltage,count
```

The ULP program counts the number of edges in the mouse wheel signal. Each edge is counted as 1/4 of a rotation. The edge count from the ULP is then divided by 4 to get the number of rotations and saved to the CSV file. Battery voltage is monitored to track power levels.

### CSV Naming

The CSV file is named as "WHEEL_YYYYMMDD_HHMMSS.csv", where `YYYYMMDD` is the date, `HHMMSS` is the time, and the `_` is a separator. A new file is created each day.

## Hublink

[Hublink.cloud](https://hublink.cloud) is meant to transfer SD card content to the cloud. Your lab will have a dashboard link that should be for internal use only. The [Hublink Docs](https://hublink.cloud/docs) contain information about how format the `meta.json` file on the SD card, which is critical for:

1. Setting up a path structure for the cloud storage.
2. Setting subject data that can be used to track the experiments.
3. Adjust settings of the Hublink sync process.

### meta.json

```json
{
  "hublink": {
    "advertise": "HUBLINK",
    "try_reconnect": false,
    "upload_path": "/WHEEL",
    "append_path": "subject:id/experimenter:name",
    "disable": false
  },
  "wheel": {
    "sleep_time_seconds": 60,
    "sync_every_minutes": 360,
    "sync_for_seconds": 30
  },
  "subject": {
    "id": "mouse001",
    "strain": "C57BL/6",
    "sex": "male"
  },
  "experimenter": {
    "name": "john_doe"
  }
}
```

### Hublink RTC Syncing

When connecting to Hublink, the RTC will be set to the current time via the `onTimestampReceived` callback.

## License

This project is licensed under the MIT License. 