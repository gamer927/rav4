// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Host mock of the Arduino API: just enough to run tcu.ino in the system test
// (test/system/system_test.cpp).
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define FALLING 2
#define HEX 16
#define DEC 10
#define F(x) (x)
#define ARDUINO 10819
#define naked
static const uint8_t A0 = 54, A1 = 55, A2 = 56, A3 = 57, A4 = 58, A5 = 59, A6 = 60, A7 = 61;
extern uint32_t g_ms;
extern int g_pin[100];       // digital levels the sketch reads
extern int g_out[100];       // digital levels the sketch wrote
extern int (*g_analog)(uint8_t);
inline uint32_t millis() { return g_ms; }
inline uint32_t micros() { return g_ms * 1000u; }
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}
static uint8_t g_port = 0xFF;
#define digitalPinToBitMask(p) ((uint8_t)1)
#define digitalPinToPort(p) (0)
#define portInputRegister(p) (&g_port)
#define portModeRegister(p) (&g_port)
#define portOutputRegister(p) (&g_port)
inline void digitalWrite(uint8_t p, uint8_t v) { g_out[p] = v; }
inline int digitalRead(uint8_t p) { return g_pin[p]; }
inline void pinMode(uint8_t, uint8_t) {}
inline int analogRead(uint8_t p) { return g_analog(p); }
inline void noInterrupts() {}
inline void interrupts() {}
#define digitalPinToInterrupt(p) (p)
inline void attachInterrupt(uint8_t, void (*)(), int) {}
inline char* ltoa(long v, char* b, int) { sprintf(b, "%ld", v); return b; }
struct HardwareSerial {
  std::string out;
  bool echo = false;
  void begin(long) {}
  void flush() {}
  int availableForWrite() { return 63; }
  void write(uint8_t c) { out += (char)c; }
  void put(const std::string& s) { out += s; if (echo) fputs(s.c_str(), stdout); }
  void print(const char* s) { put(s); }
  void print(char c) { put(std::string(1, c)); }
  void print(long v, int base = DEC) { char b[24]; sprintf(b, base == HEX ? "%lX" : "%ld", v); put(b); }
  void print(int v) { print((long)v); }
  void print(unsigned v) { print((long)v); }
  void print(unsigned long v) { print((long)v); }
  void print(uint8_t v, int base = DEC) { print((long)v, base); }
  void print(bool v) { print((long)v); }
  void print(double v, int d = 2) { char b[32]; sprintf(b, "%.*f", d, v); put(b); }
  template <typename T> void println(T v) { print(v); put("\n"); }
  template <typename T> void println(T v, int b) { print(v, b); put("\n"); }
};
extern HardwareSerial Serial, Serial1;
