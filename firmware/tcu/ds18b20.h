// DS18B20 1-Wire temperature probe (ATF temperature in the cooler line).
//
// Decoding is pure and unit-tested (test/test_tcu_logic.cpp). The bus driver
// (Ds18b20Bus) bit-bangs one pin with an external 4.7k pull-up to 5 V. Each
// bit slot disables interrupts for at most ~70 us, and a full reading blocks
// the loop for ~7 ms once a second.
#pragma once
#include <stdint.h>

namespace ds18b20 {

// Dallas/Maxim 1-Wire CRC-8 (x^8 + x^5 + x^4 + 1, reflected).
inline uint8_t crc8(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    uint8_t b = *data++;
    for (uint8_t i = 0; i < 8; i++) {
      const uint8_t mix = (crc ^ b) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      b >>= 1;
    }
  }
  return crc;
}

enum Result : uint8_t {
  OK,
  NO_DEVICE,       // bus reads all 1s (open / unplugged) or all 0s (shorted)
  BAD_CRC,         // noise on the line
  POWER_ON_VALUE,  // exactly 85.0 C: may be the reset value, not a measurement
};

// Decode the 9-byte scratchpad. On OK or POWER_ON_VALUE, `celsius` is set.
inline Result decode(const uint8_t* sp, float& celsius) {
  bool ones = true, zeros = true;
  for (uint8_t i = 0; i < 9; i++) {
    if (sp[i] != 0xFF) ones = false;
    if (sp[i] != 0x00) zeros = false;
  }
  if (ones || zeros) return NO_DEVICE;
  if (crc8(sp, 8) != sp[8]) return BAD_CRC;
  const int16_t raw = (int16_t)((uint16_t)sp[1] << 8 | sp[0]);
  celsius = raw / 16.0f;
  return raw == 0x0550 ? POWER_ON_VALUE : OK;
}

}  // namespace ds18b20

#ifdef ARDUINO
class Ds18b20Bus {
 public:
  void begin(uint8_t pin);
  bool reset();  // true if a device answered with a presence pulse
  void writeByte(uint8_t b);
  uint8_t readByte();

 private:
  void release();
  void driveLow();
  bool sample() const;
  volatile uint8_t* in_ = nullptr;
  volatile uint8_t* mode_ = nullptr;
  volatile uint8_t* out_ = nullptr;
  uint8_t mask_ = 0;
};
#endif
