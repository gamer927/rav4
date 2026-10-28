// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
#include "imu.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace {
constexpr uint8_t REG_CONFIG = 0x1A;        // digital low-pass filter
constexpr uint8_t REG_GYRO_CONFIG = 0x1B;   // full-scale range
constexpr uint8_t REG_GYRO_XOUT_H = 0x43;  // then Y at 0x45, Z at 0x47
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr float LSB_PER_DPS = 65.5f;        // +/-500 deg/s range
}  // namespace

bool Imu::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool Imu::begin(uint8_t addr) {
  addr_ = addr;
  Wire.begin();
  Wire.setClock(100000);
  Wire.setWireTimeout(3000, true);  // a bad cable must never hang the TCU
  present_ = writeReg(REG_PWR_MGMT_1, 0x01) &&  // wake, clock from gyro X PLL
             writeReg(REG_CONFIG, 0x03) &&      // ~44 Hz low-pass: ignores road buzz
             writeReg(REG_GYRO_CONFIG, 0x08);   // +/-500 deg/s
  return present_;
}

bool Imu::readYawDps(float& dps) {
  if (!present_) return false;
  Wire.beginTransmission(addr_);
  Wire.write((uint8_t)(REG_GYRO_XOUT_H + 2 * cfg::GYRO_AXIS));
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr_, (uint8_t)2) != 2) return false;
  const uint8_t hi = Wire.read();
  const uint8_t lo = Wire.read();
  dps = (int16_t)((uint16_t)hi << 8 | lo) / LSB_PER_DPS;
  return true;
}
