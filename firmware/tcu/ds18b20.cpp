#include <Arduino.h>

#include "ds18b20.h"

// Open-drain style: the line is only ever pulled low or released to the
// external pull-up, never driven high. Timings follow Maxim's 1-Wire spec.

void Ds18b20Bus::begin(uint8_t pin) {
  mask_ = digitalPinToBitMask(pin);
  const uint8_t port = digitalPinToPort(pin);
  in_ = portInputRegister(port);
  mode_ = portModeRegister(port);
  out_ = portOutputRegister(port);
  noInterrupts();
  *out_ &= ~mask_;  // when driven, drive low
  *mode_ &= ~mask_;  // released
  interrupts();
}

inline void Ds18b20Bus::release() { *mode_ &= ~mask_; }
inline void Ds18b20Bus::driveLow() { *mode_ |= mask_; }
inline bool Ds18b20Bus::sample() const { return (*in_ & mask_) != 0; }

bool Ds18b20Bus::reset() {
  // A shorted line never comes back high: give up instead of hanging.
  uint8_t tries = 125;
  while (!sample()) {
    if (--tries == 0) return false;
    delayMicroseconds(2);
  }
  noInterrupts();
  driveLow();
  interrupts();
  delayMicroseconds(480);
  noInterrupts();
  release();
  delayMicroseconds(70);
  const bool present = !sample();
  interrupts();
  delayMicroseconds(410);
  return present;
}

void Ds18b20Bus::writeByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++, b >>= 1) {
    noInterrupts();
    driveLow();
    if (b & 1) {
      delayMicroseconds(6);
      release();
      interrupts();
      delayMicroseconds(64);
    } else {
      delayMicroseconds(60);
      release();
      interrupts();
      delayMicroseconds(10);
    }
  }
}

uint8_t Ds18b20Bus::readByte() {
  uint8_t b = 0;
  for (uint8_t i = 0; i < 8; i++) {
    noInterrupts();
    driveLow();
    delayMicroseconds(3);
    release();
    delayMicroseconds(10);
    if (sample()) b |= 1 << i;
    interrupts();
    delayMicroseconds(53);
  }
  return b;
}
