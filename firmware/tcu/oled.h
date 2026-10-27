// Minimal text driver for a 128x64 I2C OLED (SSD1306, SH1106 or SSD1309).
// Layout: one double-size line (10 chars) on pages 0-1, then six normal
// lines (21 chars) on pages 2-7.
// Hero layout (setHero): one giant character (6x: 30 x 42 px) on the left of
// pages 0-5, the double-size line (7 chars) and lines 2-5 (14 chars) to its
// right, lines 6-7 full width. Made for reading the gear at a glance.
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
  static constexpr uint8_t FIRST_LINE = 2;  // rows 2..7 are normal-size lines
  static constexpr uint8_t HERO_X = 40;     // hero layout: text starts here
  static constexpr uint8_t HERO_BIG_CHARS = 7;
  static constexpr uint8_t HERO_LINE_CHARS = 14;

  void begin(uint8_t addr, OledType type);
  bool present() const { return present_; }
  void setBig(const char* text);
  void setHero(char c);  // '\0' = normal layout
  void setLine(uint8_t row, const char* text);
  void setInvert(bool on);  // lamp test
  void service(uint32_t nowMs);

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
  char big_[BIG_CHARS + 1] = {};
  char line_[8][LINE_CHARS + 1] = {};
  uint8_t dirty_ = 0xFF;  // one bit per page
  bool busy_ = false;
  uint8_t page_ = 0;
  uint8_t col_ = 0;
};
