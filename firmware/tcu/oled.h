// Minimal text driver for a 128x64 I2C OLED (SSD1306 or SH1106).
// Layout: one double-size line (10 chars) on pages 0-1, then six normal
// lines (21 chars) on pages 2-7. No frame buffer: glyphs are rendered on the
// fly and sent in 16-byte chunks, one chunk per service() call, so the
// display never blocks the control loop for more than ~2 ms.
#pragma once
#include <stdint.h>

class Oled {
 public:
  static constexpr uint8_t BIG_CHARS = 10;
  static constexpr uint8_t LINE_CHARS = 21;
  static constexpr uint8_t FIRST_LINE = 2;  // rows 2..7 are normal-size lines

  void begin(uint8_t addr, bool sh1106);
  bool present() const { return present_; }
  void setBig(const char* text);
  void setLine(uint8_t row, const char* text);
  void setInvert(bool on);  // lamp test
  void service(uint32_t nowMs);

 private:
  bool probe();
  bool init();
  bool command(const uint8_t* cmds, uint8_t n);
  uint8_t columnByte(uint8_t page, uint8_t x) const;

  uint8_t addr_ = 0x3C;
  bool sh1106_ = false;
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
