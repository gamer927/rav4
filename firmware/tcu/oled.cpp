#include "oled.h"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "font5x7.h"

namespace {

constexpr uint8_t kChunk = 16;
constexpr uint32_t kReprobeMs = 2000;

uint8_t glyphColumn(char c, uint8_t col) {
  if (col >= 5) return 0;  // 1-pixel gap between characters
  if ((uint8_t)c < FONT_FIRST || (uint8_t)c > FONT_LAST) c = '?';
  return pgm_read_byte(&FONT5X7[((uint8_t)c - FONT_FIRST) * 5 + col]);
}

// Double a 4-bit column slice vertically into 8 pixels (for 2x text).
uint8_t stretch(uint8_t nibble) {
  uint8_t r = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (nibble & (1 << i)) r |= 3 << (2 * i);
  }
  return r;
}

void copyPadded(char* dst, const char* src, uint8_t width) {
  uint8_t i = 0;
  for (; i < width && src[i]; i++) dst[i] = src[i];
  for (; i < width; i++) dst[i] = ' ';
  dst[width] = '\0';
}

}  // namespace

void Oled::begin(uint8_t addr, OledType type) {
  addr_ = addr;
  type_ = type;
  Wire.begin();
  Wire.setClock(100000);           // runs of up to ~1 m to the dash
  Wire.setWireTimeout(3000, true);  // a bad cable must never hang the TCU
  copyPadded(big_, "", BIG_CHARS);
  for (uint8_t r = 0; r < 8; r++) copyPadded(line_[r], "", LINE_CHARS);
  present_ = probe() && init();
  lastProbeMs_ = millis();
}

bool Oled::probe() {
  Wire.beginTransmission(addr_);
  return Wire.endTransmission() == 0;
}

bool Oled::command(const uint8_t* cmds, uint8_t n) {
  Wire.beginTransmission(addr_);
  Wire.write((uint8_t)0x00);  // control byte: command stream
  Wire.write(cmds, n);
  return Wire.endTransmission() == 0;
}

bool Oled::init() {
  static const uint8_t kCommon[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0xA1, 0xC8,
                                    0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4};
  static const uint8_t kPumpSsd1306[] = {0x8D, 0x14};  // internal charge pump on
  static const uint8_t kPumpSh1106[] = {0xAD, 0x8B};   // DC-DC on
  // SSD1309: no internal charge pump (the module supplies the panel voltage),
  // otherwise the same command set as the SSD1306.
  const uint8_t tail[] = {(uint8_t)(invert_ ? 0xA7 : 0xA6), 0xAF};
  bool ok = command(kCommon, sizeof(kCommon));
  if (ok && type_ == OLED_SSD1306) ok = command(kPumpSsd1306, 2);
  if (ok && type_ == OLED_SH1106) ok = command(kPumpSh1106, 2);
  ok = ok && command(tail, 2);
  dirty_ = 0xFF;
  busy_ = false;
  invertDirty_ = false;
  return ok;
}

void Oled::setBig(const char* text) {
  char tmp[BIG_CHARS + 1];
  copyPadded(tmp, text, BIG_CHARS);
  if (strcmp(tmp, big_) == 0) return;
  memcpy(big_, tmp, sizeof(tmp));
  dirty_ |= 0x03;
}

void Oled::setHero(char c) {
  if (c == hero_) return;
  hero_ = c;
  dirty_ |= 0x3F;  // pages 0-5 change layout
}

void Oled::setLine(uint8_t row, const char* text) {
  if (row < FIRST_LINE || row > 7) return;
  char tmp[LINE_CHARS + 1];
  copyPadded(tmp, text, LINE_CHARS);
  if (strcmp(tmp, line_[row]) == 0) return;
  memcpy(line_[row], tmp, sizeof(tmp));
  dirty_ |= 1 << row;
}

void Oled::setInvert(bool on) {
  if (on == invert_) return;
  invert_ = on;
  invertDirty_ = true;
}

uint8_t Oled::columnByte(uint8_t page, uint8_t x) const {
  if (hero_ && page < 6) {
    if (x < HERO_X) {
      // 5x7 glyph scaled 6x: columns 2..31, rows 3..44.
      if (x < 2 || x >= 32) return 0;
      const uint8_t g = glyphColumn(hero_, (x - 2) / 6);
      uint8_t out = 0;
      for (uint8_t bit = 0; bit < 8; bit++) {
        const int8_t gy = (int8_t)(page * 8 + bit) - 3;
        if (gy >= 0 && gy < 42 && (g >> (gy / 6)) & 1) out |= 1 << bit;
      }
      return out;
    }
    x -= HERO_X;
    if (page < FIRST_LINE) {
      const uint8_t idx = x / 12;
      if (idx >= HERO_BIG_CHARS) return 0;
      const uint8_t b = glyphColumn(big_[idx], (x % 12) / 2);
      return stretch(page == 0 ? (b & 0x0F) : (b >> 4));
    }
    const uint8_t idx = x / 6;
    if (idx >= HERO_LINE_CHARS) return 0;
    return glyphColumn(line_[page][idx], x % 6);
  }
  if (page < FIRST_LINE) {
    const uint8_t idx = x / 12;
    if (idx >= BIG_CHARS) return 0;
    const uint8_t b = glyphColumn(big_[idx], (x % 12) / 2);
    return stretch(page == 0 ? (b & 0x0F) : (b >> 4));
  }
  const uint8_t idx = x / 6;
  if (idx >= LINE_CHARS) return 0;
  return glyphColumn(line_[page][idx], x % 6);
}

void Oled::service(uint32_t nowMs) {
  if (!present_) {
    // Hot-plug / recovery: retry every couple of seconds.
    if ((uint32_t)(nowMs - lastProbeMs_) >= kReprobeMs) {
      lastProbeMs_ = nowMs;
      present_ = probe() && init();
    }
    return;
  }
  if (invertDirty_) {
    const uint8_t c = invert_ ? 0xA7 : 0xA6;
    invertDirty_ = false;
    if (!command(&c, 1)) present_ = false;
    return;
  }
  if (!busy_) {
    if (!dirty_) return;
    while (!(dirty_ & (1 << page_))) page_ = (page_ + 1) & 7;
    dirty_ &= ~(1 << page_);  // a change made mid-write re-marks it
    col_ = 0;
    busy_ = true;
  }
  const uint8_t hwCol = col_ + (type_ == OLED_SH1106 ? 2 : 0);  // SH1106 RAM is 132 wide, centred
  const uint8_t addrCmd[] = {(uint8_t)(0xB0 | page_), (uint8_t)(hwCol & 0x0F), (uint8_t)(0x10 | (hwCol >> 4))};
  bool ok = command(addrCmd, sizeof(addrCmd));
  if (ok) {
    Wire.beginTransmission(addr_);
    Wire.write((uint8_t)0x40);  // control byte: data stream
    for (uint8_t i = 0; i < kChunk; i++) Wire.write(columnByte(page_, col_ + i));
    ok = Wire.endTransmission() == 0;
  }
  if (!ok) {
    present_ = false;  // unplugged or bus fault: stop and re-probe later
    lastProbeMs_ = nowMs;
    return;
  }
  col_ += kChunk;
  if (col_ >= 128) {
    busy_ = false;
    page_ = (page_ + 1) & 7;
  }
}
