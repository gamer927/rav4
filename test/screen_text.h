// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Reads the dash screen's pixels back as text, for the system test and the
// chip test: what the driver would see, not what the firmware meant to show.
//
// Every character cell is matched pixel for pixel against the font, in the
// three sizes the firmware draws: small 6 px cells (from x 0, or from x 40
// beside the gear), double-size 12 px cells over two pages, and the 6x gear
// digit. A cell that isn't exactly a character reads as '~'.
//
// px(page, x) returns the column byte at that page (0-7) and x (0-127).
#pragma once
#include <string.h>

#include <string>

#include "font5x7.h"

namespace screen_text {

inline char glyph(const uint8_t cols[5]) {
  for (int c = FONT_FIRST; c <= FONT_LAST; c++) {
    if (!memcmp(cols, &FONT5X7[(c - FONT_FIRST) * 5], 5)) return (char)c;
  }
  return '~';
}

template <class Px>
std::string small(Px px, int page, int x0, int n) {
  std::string s;
  for (int i = 0; i < n; i++) {
    const int x = x0 + 6 * i;
    if (x + 5 >= 128) break;
    uint8_t cols[5];
    for (int c = 0; c < 5; c++) cols[c] = px(page, x + c);
    s += px(page, x + 5) ? '~' : glyph(cols);
  }
  return s;
}

template <class Px>
std::string big(Px px, int page, int x0, int n) {
  std::string s;
  for (int i = 0; i < n; i++) {
    uint8_t cols[5];
    bool ok = true;
    for (int c = 0; c < 5; c++) {
      const int x = x0 + 12 * i + 2 * c;
      if (x + 1 >= 128) return s;
      const uint8_t top = px(page, x), bot = px(page + 1, x);
      ok = ok && top == px(page, x + 1) && bot == px(page + 1, x + 1);
      uint8_t g = 0;
      for (int b = 0; b < 4; b++) {
        if ((top >> (2 * b)) & 1) g |= 1 << b;
        if ((bot >> (2 * b)) & 1) g |= 1 << (b + 4);
      }
      cols[c] = g;
    }
    s += ok ? glyph(cols) : '~';
  }
  return s;
}

// The gear: a 5x7 glyph scaled 6x, from x 2 and y 3 (sampled mid-cell).
template <class Px>
char gear(Px px) {
  uint8_t cols[5] = {};
  for (int c = 0; c < 5; c++) {
    for (int r = 0; r < 7; r++) {
      const int x = 2 + 6 * c + 3, y = 3 + 6 * r + 3;
      if ((px(y / 8, x) >> (y % 8)) & 1) cols[c] |= 1 << r;
    }
  }
  return glyph(cols);
}

// Everything readable on the screen, as one string: "[gear 3] ...|...|".
template <class Px>
std::string read(Px px) {
  std::string s = "[gear ";
  s += gear(px);
  s += "] ";
  for (int p = 0; p < 8; p++) {
    s += small(px, p, 0, 21) + "|" + small(px, p, 40, 14) + "|";
    if (p % 2 == 0) s += big(px, p, 0, 10) + "|" + big(px, p, 40, 7) + "|";
  }
  return s;
}

}  // namespace screen_text
