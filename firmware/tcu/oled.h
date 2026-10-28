// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Minimal text driver for a 128x64 I2C OLED (SSD1306, SH1106 or SSD1309).
// Eight rows of normal text (21 chars, 3 mm tall on a 2.42" screen). Any
// pair of rows (0-1, 2-3, 4-5, 6-7) can instead hold one double-size line
// (10 chars, 6 mm tall): setBigLine(). Rows 0-1 start out double-size.
// Hero layout (setHero): one giant character (6x: 30 x 42 px, 18 mm) on the
// left of rows 0-5, with 7 double-size or 14 normal chars to its right;
// rows 6-7 stay full width. Made for reading the gear at a glance.
// No frame buffer: glyphs are rendered on the fly and sent in 16-byte chunks,
// one chunk per service() call, so the display never blocks the control loop
// for more than ~2 ms.
#pragma once
#include <stdint.h>

enum OledType : uint8_t { OLED_SSD1306, OLED_SH1106, OLED_SSD1309 };

class Oled {
 public:
  static constexpr uint8_t BIG_CHARS = 10;
  static constexpr uint8_t LINE_CHARS = 21;
  static constexpr uint8_t HERO_X = 40;     // hero layout: text starts here
  static constexpr uint8_t HERO_BIG_CHARS = 7;
  static constexpr uint8_t HERO_LINE_CHARS = 14;

  void begin(uint8_t addr, OledType type);
  bool present() const { return present_; }
  void setBig(const char* text) { setBigLine(0, text); }
  void setBigLine(uint8_t row, const char* text);  // row 0, 2, 4 or 6 (and the next)
  void setHero(char c);  // '\0' = normal layout
  void setLine(uint8_t row, const char* text);   // row 0-7, normal size
  void setInvert(bool on);  // lamp test
  void service(uint32_t nowMs);
  const char* debugLine(uint8_t row) const { return row < 8 ? line_[row] : ""; }  // for the system test

 private:
  bool probe();
  bool init();
  bool command(const uint8_t* cmds, uint8_t n);
  uint8_t columnByte(uint8_t page, uint8_t x) const;

  uint8_t addr_ = 0x3C;
  OledType type_ = OLED_SSD1306;
  char hero_ = '\0';
  bool present_ = false;
  uint32_t lastProbeMs_ = 0;
  bool invert_ = false;
  bool invertDirty_ = false;
  char line_[8][LINE_CHARS + 1] = {};
  uint8_t bigPairs_ = 0x01;  // one bit per row pair: double-size text
  uint8_t dirty_ = 0xFF;  // one bit per page
  bool busy_ = false;
  uint8_t page_ = 0;
  uint8_t col_ = 0;
};
