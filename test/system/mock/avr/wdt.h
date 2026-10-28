// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Mock watchdog: records when it was last kicked, so the system test can
// check the loop never goes longer than the 250 ms timeout without a kick.
// Arming the 15 ms reset (what the watchdog's ISR does) throws WatchdogReset:
// the system test catches it and reboots the firmware.
#pragma once
#include <stdint.h>
#define WDTO_15MS 0
#define WDTO_250MS 4
#define WDRF 3
#define WDIE 6
#define _BV(b) (1 << (b))
struct WatchdogReset {};
extern uint8_t MCUSR, WDTCSR;
extern uint32_t g_ms, g_wdtLastKick, g_wdtMaxGap;
extern bool g_wdtEnabled;
inline void wdt_enable(int timeout) {
  if (timeout == WDTO_15MS) throw WatchdogReset();
  g_wdtEnabled = true;
  g_wdtLastKick = g_ms;
  WDTCSR = 0;  // like avr-libc: interrupt mode off
}
inline void wdt_disable() { g_wdtEnabled = false; WDTCSR = 0; }
inline void wdt_reset() {
  if (g_wdtEnabled && g_ms - g_wdtLastKick > g_wdtMaxGap) g_wdtMaxGap = g_ms - g_wdtLastKick;
  g_wdtLastKick = g_ms;
}
