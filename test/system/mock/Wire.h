// Mock I2C bus with a simulated SSD1306 (page addressing) and MPU-6050 behind
// it. unplugged = true makes every transaction fail (a cut screen cable).
#pragma once
#include <stdint.h>
#include <stddef.h>
struct TwoWire {
  uint8_t fb[8][132] = {};
  bool inverted = false;
  uint8_t page = 0, col = 0;
  uint8_t buf[64];
  uint8_t n = 0;
  int transactions = 0, maxBytes = 0;
  bool unplugged = false;
  void begin() {}
  void setClock(uint32_t) {}
  void setWireTimeout(uint32_t, bool) {}
  void beginTransmission(uint8_t) { n = 0; }
  size_t write(uint8_t b) { buf[n++] = b; return 1; }
  size_t write(const uint8_t* d, size_t k) { for (size_t i = 0; i < k; i++) write(d[i]); return k; }
  int16_t yawRaw = 0;
  uint8_t rx[2];
  uint8_t rxPos = 2;
  uint8_t requestFrom(uint8_t addr, uint8_t n) {
    if (unplugged || addr != 0x68 || n != 2) return 0;
    rx[0] = (uint8_t)((uint16_t)yawRaw >> 8);
    rx[1] = (uint8_t)(yawRaw & 0xFF);
    rxPos = 0;
    return 2;
  }
  int read() { return rxPos < 2 ? rx[rxPos++] : -1; }
  uint8_t endTransmission(bool) { return endTransmission(); }
  uint8_t endTransmission() {
    transactions++;
    if (unplugged) return 2;  // address NACK
    if (n > maxBytes) maxBytes = n;
    if (n == 0) return 0;
    if (buf[0] == 0x00) {
      for (uint8_t i = 1; i < n; i++) {
        const uint8_t c = buf[i];
        if (c >= 0xB0 && c <= 0xB7) page = c & 7;
        else if (c <= 0x0F) col = (col & 0xF0) | c;
        else if (c >= 0x10 && c <= 0x1F) col = (col & 0x0F) | ((c & 0x0F) << 4);
        else if (c == 0xA6) inverted = false;
        else if (c == 0xA7) inverted = true;
        else if (c == 0x8D || c == 0xD5 || c == 0xA8 || c == 0xD3 || c == 0xDA || c == 0x81 || c == 0xD9 || c == 0xDB || c == 0xAD) i++;  // 2-byte cmds
      }
    } else if (buf[0] == 0x40) {
      for (uint8_t i = 1; i < n; i++) { if (col < 132) fb[page][col] = buf[i]; col++; }
    }
    return 0;
  }
};
extern TwoWire Wire;
