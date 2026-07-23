#pragma once

#include <Wire.h>

/// Minimal LSM303AGR magnetometer reader over I2C (ESP32-safe; no STM32duino dependency).
class Lsm303Mag {
public:
  static constexpr uint8_t kI2cAddr7 = 0x1E;
  static constexpr uint8_t kWhoAmIReg = 0x4F;
  static constexpr uint8_t kExpectedWhoAmI = 0x40;
  static constexpr uint8_t kCfgRegA = 0x60;
  static constexpr uint8_t kCfgRegC = 0x62;
  static constexpr uint8_t kOutXRegL = 0x68;

  /// HR mode, 100 Hz ODR, BDU enabled, continuous mode.
  bool begin(TwoWire &wire) {
    wire_ = &wire;

    uint8_t whoAmI = 0;
    if (!readReg(kWhoAmIReg, whoAmI) || whoAmI != kExpectedWhoAmI) {
      return false;
    }

    if (!writeReg(kCfgRegC, 0x10)) { // BDU enabled
      return false;
    }

    // MD=continuous (0), ODR=100 Hz (0x0C), LP=HR (0).
    if (!writeReg(kCfgRegA, 0x0C)) {
      return false;
    }

    return true;
  }

  bool readRaw(int16_t xyz[3]) {
    uint8_t buf[6] = {};
    if (!readBytes(kOutXRegL, buf, sizeof(buf))) {
      return false;
    }

    xyz[0] = static_cast<int16_t>(static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8));
    xyz[1] = static_cast<int16_t>(static_cast<uint16_t>(buf[2]) | (static_cast<uint16_t>(buf[3]) << 8));
    xyz[2] = static_cast<int16_t>(static_cast<uint16_t>(buf[4]) | (static_cast<uint16_t>(buf[5]) << 8));
    return true;
  }

private:
  TwoWire *wire_ = nullptr;

  bool writeReg(uint8_t reg, uint8_t value) {
    wire_->beginTransmission(kI2cAddr7);
    wire_->write(reg);
    wire_->write(value);
    return wire_->endTransmission() == 0;
  }

  bool readReg(uint8_t reg, uint8_t &value) {
    return readBytes(reg, &value, 1);
  }

  bool readBytes(uint8_t reg, uint8_t *buffer, size_t length) {
    wire_->beginTransmission(kI2cAddr7);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) {
      return false;
    }

    if (wire_->requestFrom(kI2cAddr7, static_cast<uint8_t>(length)) != length) {
      return false;
    }

    for (size_t i = 0; i < length; ++i) {
      buffer[i] = wire_->read();
    }
    return true;
  }
};
