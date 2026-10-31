// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Chip test: the firmware exactly as you flash it (the .hex the Arduino build
// makes, with the real Arduino core and Wire library), started by the real
// Mega 2560 bootloader (stk500v2), running on a simulated ATmega2560 (simavr):
// every instruction, timer, interrupt, the watchdog, the ADC, the UART and
// the I2C hardware are the chip's, not stand-ins.
//
// Around the chip, the board and the car, modelled from the circuit
// (tools/boards.py) and the car, never from the firmware's own settings:
//   * switches and optos pull their pins low; relays pull in only when D6
//     drives the ULN2803 AND the ON/OFF switch's power pole feeds the coils
//   * solenoid current through each ACS712 (185 mV/A around 2.5 V)
//   * the 47k/10k dividers on the battery and the coil supply
//   * speed and tach pulses on D2 / D3, timed in CPU cycles
//   * the screen: an SSD1309 on I2C, whose pixels are read back as text
//   * the gyro: an MPU-6050 on I2C
//   * the ATF probe: a DS18B20 answering on the 1-Wire line, bit by bit
//
// The firmware is a black box here: everything is judged from its pins,
// its USB log and the pixels on the screen, as you would on the bench.
//
// Built and run by `make chip`.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <simavr/avr_adc.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_twi.h>
#include <simavr/avr_uart.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_hex.h>
#include <simavr/sim_irq.h>
#include <simavr/sim_time.h>

#include "pins.h"
#include "screen_text.h"  // (avr/pgmspace.h for the font: the system test's PC version)

static const uint32_t F_CPU = 16000000;
static const uint64_t CYCLES_PER_MS = F_CPU / 1000;

static int g_checks = 0, g_failures = 0;
static avr_t* avr;
static std::string g_log;  // everything the firmware sent over USB (and the bootloader)
uint64_t nowMs() { return avr->cycle / CYCLES_PER_MS; }
static std::string screenText();

#define CHECK(cond)                                                                                   \
  do {                                                                                                \
    g_checks++;                                                                                       \
    if (!(cond)) {                                                                                    \
      g_failures++;                                                                                   \
      printf("    FAIL line %d: %s  (t=%llu ms)\n      screen: %s\n", __LINE__, #cond,                \
             (unsigned long long)nowMs(), screenText().c_str());                                      \
    }                                                                                                 \
  } while (0)

// ------------------------------------------------------------------ the car
// Car facts, from the car (not from config.h): what a 1999 RAV4 does.
static const float CAR_PULSES_PER_MILE = 4101.0f;  // speed sensor
static const float CAR_TACH_PER_REV = 2.0f;        // 4 cylinders: 2 sparks per turn
static const float CAR_RPM_PER_MPH[5] = {0, 151.0f, 83.2f, 53.8f, 39.4f};
static const float SOLENOID_OHMS = 12.5f;          // 11-15 ohm shift solenoids
static const float DIVIDER = 10.0f / (47.0f + 10.0f);  // RB1/RB2 and RK1/RK2 on the board
static const float ACS_V_PER_A = 0.185f;
static const float ACS_ZERO_V[3] = {2.54f, 2.47f, 2.52f};  // never exactly 2.50 V on a real part
static const float TPS_CLOSED_V = 0.60f, TPS_WOT_V = 4.00f;

struct Car {
  float mph = 0, tps = 0, atfC = 70, battV = 12.6f, yawDps = 0;
  bool engineRunning = false, cranking = false;
  bool leverInD = false, brake = false;
  bool switchOn = false;  // the ON/OFF switch (both poles)
  bool eco = true, sport = false, manual = false;
  bool up = false, down = false, test = false;
  // faults
  bool coilFuseBlown = false, cutBroken = false, readPoleShorted = false;
  bool welded[3] = {false, false, false};
  bool solenoidOpen[3] = {false, false, false};
  bool screenUnplugged = false, probeUnplugged = false, gyroDead = false;
  int probeBadReads = 0;       // the next n readings arrive with a bad checksum (noise on the cable)
  bool probeLostPower = false;  // the probe blinked off: its next reading is the power-on 85.0 C
  bool vssLost = false, tachLost = false;
} car;

// Toyota A240/A540 solenoid table: 1st S1, 2nd S1+S2, 3rd S2, 4th none.
static void solenoidsForGear(int g, bool& s1, bool& s2) {
  s1 = g == 1 || g == 2;
  s2 = g == 2 || g == 3;
}
static int gearFromSolenoids(bool s1, bool s2) { return s1 ? (s2 ? 2 : 1) : (s2 ? 3 : 4); }
static int ecuGear() { return car.mph < 12 ? 1 : car.mph < 24 ? 2 : car.mph < 40 ? 3 : 4; }
static bool ecuLockup() { return ecuGear() >= 3 && car.mph > 40 && !car.brake; }

// ------------------------------------------------------------------ the chip's pins
static avr_irq_t* pinIrq(char port, int bit) { return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(port), bit); }
static void drive(char port, int bit, int level) { avr_raise_irq(pinIrq(port, bit), level ? 1 : 0); }
// What a pin drives: 1 only if it's an output and high. A pin left as an
// input (reset, before setup()) is high-Z: the ULN2803's own pull-down = off.
static bool out(char port, int bit) {
  avr_ioport_state_t st;
  if (avr_ioctl(avr, AVR_IOCTL_IOPORT_GETSTATE(port), &st) != 0) return false;
  return ((st.ddr >> bit) & 1) && ((st.port >> bit) & 1);
}
#define OUT(name) out(PIN_##name##_PORT, PIN_##name##_BIT)
#define DRIVE(name, level) drive(PIN_##name##_PORT, PIN_##name##_BIT, level)
static void adc(int ch, float volts) {
  if (volts < 0) volts = 0;
  if (volts > 5) volts = 5;
  avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + ch), (uint32_t)(volts * 1000 + 0.5f));
}

static bool coilPowered() { return (car.switchOn || car.cutBroken) && !car.coilFuseBlown; }
static bool relayIn(int i) { return (OUT(RELAY) && coilPowered()) || car.welded[i]; }
static bool driverOn(int i) { return i == 0 ? OUT(S1) : i == 1 ? OUT(S2) : OUT(SL); }
static float solenoidAmps(int i) {
  // The driver's +12 V only reaches the solenoid through the relay's NO_B contact.
  return driverOn(i) && relayIn(i) && !car.solenoidOpen[i] ? car.battV / SOLENOID_OHMS : 0.0f;
}
static int engagedGear() {
  bool e1, e2;
  solenoidsForGear(ecuGear(), e1, e2);
  const bool s1 = relayIn(0) ? driverOn(0) && !car.solenoidOpen[0] : e1;
  const bool s2 = relayIn(1) ? driverOn(1) && !car.solenoidOpen[1] : e2;
  return gearFromSolenoids(s1, s2);
}
static float engineRpm() {
  if (car.cranking) return 250;
  if (!car.engineRunning) return 0;
  const float rpm = car.mph * CAR_RPM_PER_MPH[engagedGear()] * 1.05f;
  return rpm < 750 ? 750 : rpm;
}
static bool tcuInControl() { return OUT(RELAY) && coilPowered(); }
static bool factorySafe() {
  return !relayIn(0) && !relayIn(1) && !relayIn(2) && solenoidAmps(0) + solenoidAmps(1) + solenoidAmps(2) < 0.1f;
}

// Every millisecond: the car's state onto the pins and the ADC.
static void applyCar() {
  DRIVE(MASTER, !(car.switchOn || car.readPoleShorted));
  DRIVE(ECO, !car.eco);
  DRIVE(SPORT, !car.sport);
  DRIVE(MANUAL, !car.manual);
  DRIVE(UP, !car.up);
  DRIVE(DOWN, !car.down);
  DRIVE(TEST, !car.test);
  DRIVE(LEVER_D, !car.leverInD);
  DRIVE(BRAKE, !car.brake);
  bool s1, s2;
  solenoidsForGear(ecuGear(), s1, s2);
  DRIVE(ECU_S1, !s1);
  DRIVE(ECU_S2, !s2);
  DRIVE(ECU_SL, !ecuLockup());
  adc(PIN_TPS_ADC, TPS_CLOSED_V + car.tps / 100.0f * (TPS_WOT_V - TPS_CLOSED_V));
  adc(PIN_TPS_E2_ADC, 0.0f);
  adc(PIN_VBAT_ADC, car.battV * DIVIDER);
  adc(PIN_COIL_SENSE_ADC, coilPowered() ? car.battV * DIVIDER : 0.0f);
  adc(PIN_IS_S1_ADC, ACS_ZERO_V[0] + ACS_V_PER_A * solenoidAmps(0));
  adc(PIN_IS_S2_ADC, ACS_ZERO_V[1] + ACS_V_PER_A * solenoidAmps(1));
  adc(PIN_IS_SL_ADC, ACS_ZERO_V[2] + ACS_V_PER_A * solenoidAmps(2));
}

// Speed and tach: square waves on D2 / D3, timed in CPU cycles.
struct Pulse {
  char port;
  int bit;
  bool level = true;
  float hz = 0;
};
static Pulse vssPulse{PIN_VSS_PORT, PIN_VSS_BIT}, tachPulse{PIN_TACH_PORT, PIN_TACH_BIT};
static avr_cycle_count_t pulseTimer(avr_t*, avr_cycle_count_t when, void* param) {
  Pulse* p = (Pulse*)param;
  if (p->hz < 0.5f) {
    if (!p->level) drive(p->port, p->bit, p->level = true);
    return when + CYCLES_PER_MS;  // check again in 1 ms
  }
  p->level = !p->level;
  drive(p->port, p->bit, p->level);
  return when + (avr_cycle_count_t)(F_CPU / p->hz / 2);
}
static void probeLine();
static avr_cycle_count_t carTimer(avr_t*, avr_cycle_count_t when, void*) {
  applyCar();
  probeLine();
  vssPulse.hz = car.vssLost ? 0 : car.mph * CAR_PULSES_PER_MILE / 3600.0f;
  tachPulse.hz = car.tachLost ? 0 : engineRpm() * CAR_TACH_PER_REV / 60.0f;
  return when + CYCLES_PER_MS;
}

// ------------------------------------------------------------------ USB serial
static void uartOut(avr_irq_t*, uint32_t value, void*) { g_log += (char)value; }

// ------------------------------------------------------------------ I2C: screen and gyro
struct Ssd1309 {
  uint8_t fb[8][128] = {};
  int page = 0, col = 0;
  bool selected = false, first = false, data = false, on = false, inverted = false;
  int argsLeft = 0;  // bytes still to come for a multi-byte command
  void command(uint8_t c) {
    if (argsLeft) {
      argsLeft--;
      return;
    }
    if (c >= 0xB0 && c <= 0xB7) page = c & 7;
    else if (c <= 0x0F) col = (col & 0xF0) | c;
    else if (c >= 0x10 && c <= 0x1F) col = (col & 0x0F) | ((c & 0x0F) << 4);
    else if (c == 0xAF) on = true;
    else if (c == 0xAE) on = false;
    else if (c == 0xA6) inverted = false;
    else if (c == 0xA7) inverted = true;
    // two-byte commands: the next byte is their argument
    else if (c == 0x81 || c == 0xA8 || c == 0xD3 || c == 0xD5 || c == 0xD9 || c == 0xDA || c == 0xDB ||
             c == 0x8D || c == 0xAD || c == 0x20)
      argsLeft = 1;
  }
} screen;

struct Mpu6050 {
  uint8_t reg = 0, regs[128] = {};
  bool selected = false, first = false;
} gyro;

static avr_irq_t* twiIn;
static uint8_t selectedAddr = 0;
static void twiOut(avr_irq_t*, uint32_t value, void*) {
  avr_twi_msg_irq_t v;
  v.u.v = value;
  const uint8_t msg = v.u.twi.msg, addr = v.u.twi.addr, data = v.u.twi.data;
  if (msg & TWI_COND_STOP) selectedAddr = 0;
  if (msg & TWI_COND_START) {
    selectedAddr = 0;
    const uint8_t a7 = addr >> 1;
    const bool present = (a7 == 0x3C && !car.screenUnplugged) || (a7 == 0x68 && !car.gyroDead);
    if (present) {
      selectedAddr = addr;
      if (a7 == 0x3C) screen.first = true;
      if (a7 == 0x68 && !(addr & 1)) gyro.first = true;
      avr_raise_irq(twiIn, avr_twi_irq_msg(TWI_COND_ACK, addr, 1));
    }
  }
  if (!selectedAddr) return;
  const uint8_t a7 = selectedAddr >> 1;
  if (msg & TWI_COND_WRITE) {
    avr_raise_irq(twiIn, avr_twi_irq_msg(TWI_COND_ACK, selectedAddr, 1));
    if (a7 == 0x3C) {
      if (screen.first) {
        screen.first = false;
        screen.data = data == 0x40;  // control byte: 0x00 commands, 0x40 pixels
      } else if (screen.data) {
        if (screen.col < 128) screen.fb[screen.page][screen.col] = data;
        screen.col++;
      } else {
        screen.command(data);
      }
    } else {
      if (gyro.first) {
        gyro.first = false;
        gyro.reg = data & 0x7F;
      } else {
        gyro.regs[gyro.reg++ & 0x7F] = data;
      }
    }
  }
  if ((msg & TWI_COND_READ) && a7 == 0x68) {
    // gyro registers 0x43..0x48: X, Y, Z rate, big-endian, 65.5 LSB per deg/s
    const int16_t z = (int16_t)lroundf(car.yawDps * 65.5f);
    gyro.regs[0x47] = (uint8_t)(z >> 8);
    gyro.regs[0x48] = (uint8_t)z;
    avr_raise_irq(twiIn, avr_twi_irq_msg(TWI_COND_READ, selectedAddr, gyro.regs[gyro.reg++ & 0x7F]));
  }
}

// What the screen shows, as text (test/screen_text.h).
static std::string screenText() {
  if (!screen.on) return "(screen off)";
  return screen_text::read([](int page, int x) { return screen.fb[page][x]; });
}
static bool screenSays(const char* words) { return screenText().find(words) != std::string::npos; }

// ------------------------------------------------------------------ 1-Wire: the DS18B20
// The firmware drives the line low by making the pin an output (its PORT
// bit stays 0), and lets the 4.7k pull it up by making it an input again.
struct Ds18b20 {
  bool masterLow = false, slaveLow = false;
  uint64_t fallAt = 0;
  enum { IDLE, ROM, FUNC, READING } state = IDLE;
  uint8_t byte = 0, bits = 0;
  uint8_t tx[9] = {};
  int txBit = 0;
  int16_t raw = 0x0550;  // 85.0 C: the power-on value, until the first conversion
  int conversions = 0;
  void line() { drive(PIN_ATF_PROBE_PORT, PIN_ATF_PROBE_BIT, !(masterLow || slaveLow)); }
} probe;

static void probeLine() { probe.line(); }
static avr_cycle_count_t probeRelease(avr_t*, avr_cycle_count_t, void*) {
  probe.slaveLow = false;
  probe.line();
  return 0;
}
static avr_cycle_count_t probePresence(avr_t*, avr_cycle_count_t, void*) {
  probe.slaveLow = true;
  probe.line();
  avr_cycle_timer_register_usec(avr, 120, probeRelease, nullptr);
  return 0;
}
static avr_cycle_count_t probeConverted(avr_t*, avr_cycle_count_t, void*) {
  probe.raw = (int16_t)lroundf(car.atfC * 16.0f);
  return 0;
}
// Maxim's CRC-8 (x^8 + x^5 + x^4 + 1), written here from the datasheet.
static uint8_t crc8(const uint8_t* d, int n) {
  uint8_t crc = 0;
  for (int i = 0; i < n; i++) {
    uint8_t b = d[i];
    for (int k = 0; k < 8; k++) {
      const bool mix = (crc ^ b) & 1;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      b >>= 1;
    }
  }
  return crc;
}
static void probeByte(uint8_t b) {
  if (probe.state == Ds18b20::ROM) {
    probe.state = b == 0xCC ? Ds18b20::FUNC : Ds18b20::IDLE;  // skip ROM
  } else if (probe.state == Ds18b20::FUNC) {
    if (b == 0x44) {  // convert: 750 ms at 12 bits
      probe.conversions++;
      avr_cycle_timer_register_usec(avr, 750000, probeConverted, nullptr);
      probe.state = Ds18b20::IDLE;
    } else if (b == 0xBE) {  // read the scratchpad
      const int16_t raw = car.probeLostPower ? 0x0550 : probe.raw;
      car.probeLostPower = false;
      const uint8_t sp[8] = {(uint8_t)raw, (uint8_t)(raw >> 8), 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10};
      memcpy(probe.tx, sp, 8);
      probe.tx[8] = crc8(sp, 8);
      if (car.probeBadReads > 0) {
        car.probeBadReads--;
        probe.tx[1] ^= 0x08;  // one bit flipped on the way: the checksum no longer matches
      }
      probe.txBit = 0;
      probe.state = Ds18b20::READING;
    } else {
      probe.state = Ds18b20::IDLE;
    }
  }
}
static void probeDdr(avr_irq_t*, uint32_t ddr, void*) {
  if (car.probeUnplugged) {
    probe.masterLow = (ddr >> PIN_ATF_PROBE_BIT) & 1;
    probe.line();
    return;
  }
  const bool low = (ddr >> PIN_ATF_PROBE_BIT) & 1;
  if (low == probe.masterLow) return;
  probe.masterLow = low;
  if (low) {  // a slot or a reset starts
    probe.fallAt = avr->cycle;
    if (probe.state == Ds18b20::READING) {
      const bool bit = (probe.tx[probe.txBit / 8] >> (probe.txBit % 8)) & 1;
      if (!bit) {  // a 0: hold the line low for 30 us
        probe.slaveLow = true;
        avr_cycle_timer_register_usec(avr, 30, probeRelease, nullptr);
      }
      if (++probe.txBit == 72) probe.state = Ds18b20::IDLE;
    }
  } else {
    const float us = (avr->cycle - probe.fallAt) / 16.0f;
    if (us >= 400) {  // reset pulse: answer with a presence pulse
      probe.state = Ds18b20::ROM;
      probe.bits = probe.byte = 0;
      avr_cycle_timer_register_usec(avr, 30, probePresence, nullptr);
    } else if (probe.state == Ds18b20::ROM || probe.state == Ds18b20::FUNC) {
      probe.byte |= (us < 15 ? 1 : 0) << probe.bits;  // short low = 1, long low = 0
      if (++probe.bits == 8) {
        probeByte(probe.byte);
        probe.bits = probe.byte = 0;
      }
    }
  }
  probe.line();
}

// ------------------------------------------------------------------ the chip
static std::string g_hex, g_boot;

static void loadHex(const char* path) {
  ihex_chunk_p chunks;
  const int n = read_ihex_chunks(path, &chunks);
  if (n <= 0) {
    printf("can't read %s\n", path);
    exit(2);
  }
  for (int i = 0; i < n; i++) memcpy(avr->flash + chunks[i].baseaddr, chunks[i].data, chunks[i].size);
  free_ihex_chunks(chunks);
}

// A reset (watchdog, reset line) stops every simavr timer, but the car and
// the board carry on: re-arm them. The chip's own reset handling runs first.
static void (*chipReset)(avr_t*) = nullptr;
static int g_resets = 0;
static void startModels() {
  avr_cycle_timer_register(avr, 1, carTimer, nullptr);
  avr_cycle_timer_register(avr, 7, pulseTimer, &vssPulse);
  avr_cycle_timer_register(avr, 11, pulseTimer, &tachPulse);
}
static void onReset(avr_t* a) {
  if (chipReset) chipReset(a);
  g_resets++;
  probe.masterLow = false;  // every pin is an input again: the master lets go
  startModels();
}

// Power on: a fresh chip (RAM all zeros), the bootloader at 0x3E000 runs
// first (the Mega's BOOTRST fuse), then starts the sketch.
static void powerOn() {
  avr = avr_make_mcu_by_name("atmega2560");
  avr_init(avr);
  avr->frequency = F_CPU;
  avr->vcc = avr->avcc = avr->aref = 5000;
  avr->log = LOG_ERROR;
  loadHex(g_hex.c_str());
  loadHex(g_boot.c_str());
  avr->reset_pc = 0x3E000;
  avr->pc = 0x3E000;
  uint32_t f = 0;
  avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS('0'), &f);
  f &= ~AVR_UART_FLAG_STDIO;
  avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS('0'), &f);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), uartOut, nullptr);
  twiIn = avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_INPUT);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_TWI_GETIRQ(0), TWI_IRQ_OUTPUT), twiOut, nullptr);
  avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(PIN_ATF_PROBE_PORT), IOPORT_IRQ_DIRECTION_ALL),
                          probeDdr, nullptr);
  // The pins the board drives are unfiltered: every update reaches the PIN
  // register, so after a reset (which clears it) the chip sees the wire's real
  // level at once, as a real chip does, not 0 until the level next changes.
  const int inputs[][2] = {{PIN_MASTER_PORT, PIN_MASTER_BIT}, {PIN_ECO_PORT, PIN_ECO_BIT},
                           {PIN_SPORT_PORT, PIN_SPORT_BIT}, {PIN_MANUAL_PORT, PIN_MANUAL_BIT},
                           {PIN_UP_PORT, PIN_UP_BIT}, {PIN_DOWN_PORT, PIN_DOWN_BIT},
                           {PIN_TEST_PORT, PIN_TEST_BIT}, {PIN_LEVER_D_PORT, PIN_LEVER_D_BIT},
                           {PIN_BRAKE_PORT, PIN_BRAKE_BIT}, {PIN_ECU_S1_PORT, PIN_ECU_S1_BIT},
                           {PIN_ECU_S2_PORT, PIN_ECU_S2_BIT}, {PIN_ECU_SL_PORT, PIN_ECU_SL_BIT},
                           {PIN_ATF_PROBE_PORT, PIN_ATF_PROBE_BIT}};
  for (const auto& in : inputs) pinIrq((char)in[0], in[1])->flags &= ~IRQ_FLAG_FILTERED;
  probe.line();
  chipReset = avr->reset;
  avr->reset = onReset;
  startModels();
  applyCar();
}

static void run(uint32_t ms) {
  const uint64_t end = avr->cycle + ms * CYCLES_PER_MS;
  while (avr->cycle < end) {
    const int st = avr_run(avr);
    if (st == cpu_Done || st == cpu_Crashed) {
      printf("    the chip stopped (state %d) at pc 0x%05x\n", st, avr->pc);
      g_failures++;
      return;
    }
  }
}
// Run until the USB log shows `text` (after `from`), or give up.
static bool runUntilLog(const char* text, uint32_t maxMs, size_t from = 0) {
  for (uint32_t t = 0; t < maxMs; t += 10) {
    if (g_log.find(text, from) != std::string::npos) return true;
    run(10);
  }
  return g_log.find(text, from) != std::string::npos;
}
static bool runUntilScreen(const char* words, uint32_t maxMs) {
  for (uint32_t t = 0; t < maxMs; t += 20) {
    if (screenSays(words)) return true;
    run(20);
  }
  return screenSays(words);
}
static void rampTo(float mph, float mphPerS = 8.0f) {
  while (fabsf(car.mph - mph) > 0.01f) {
    const float step = mphPerS / 100.0f;
    car.mph = car.mph < mph ? fminf(mph, car.mph + step) : fmaxf(mph, car.mph - step);
    run(10);
  }
}
static int count(const std::string& s, const char* what) {
  int n = 0;
  for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) n++;
  return n;
}
// The firmware's own health report (USB log): worst over the run so far.
static void health(uint32_t& slowestUs, uint32_t& marginBytes, size_t from = 0) {
  slowestUs = 0;
  marginBytes = 0xFFFF;
  for (size_t p = g_log.find("# health ", from); p != std::string::npos; p = g_log.find("# health ", p + 1)) {
    unsigned a = 0, b = 0;
    if (sscanf(g_log.c_str() + p, "# health slowest_loop_us=%u stack_margin_bytes=%u", &a, &b) == 2) {
      if (a > slowestUs) slowestUs = a;
      if (b < marginBytes) marginBytes = b;
    }
  }
}

// One row of the last self-test table in `log`: "#   SOLENOID 1 OK  0.95A" -> "OK".
static std::string bitResult(const std::string& log, const char* name) {
  const std::string key = std::string("#   ") + name + " ";
  const size_t p = log.rfind(key);
  if (p == std::string::npos) return "missing";
  char word[8] = {};
  sscanf(log.c_str() + p + key.size(), "%7s", word);
  return word;
}

// Key on, parked: the bootloader, the boot, the power-on checks.
static void keyOn() {
  powerOn();
  CHECK(runUntilLog("# TCU boot", 10000));
  run(3000);
}
static void startEngine() {
  car.cranking = true;
  car.battV = 10.8f;
  run(600);
  car.cranking = false;
  car.engineRunning = true;
  car.battV = 14.2f;
  run(500);
}
static void armAndDrive() {
  car.switchOn = false;
  run(300);
  car.switchOn = true;
  run(300);
  car.leverInD = true;
  run(500);
}

// ------------------------------------------------------------------ scenarios: as shipped
// The .hex as it comes out of the Arduino build: CALIBRATED = false, so the
// TCU must never take over; the self-test and the screen must work.

static void s_boot_through_bootloader() {
  powerOn();
  // The bootloader runs first: it waits about 0.8 s for a programmer before
  // it starts the sketch (the sketch's first line comes after that wait).
  CHECK(runUntilLog("# TCU boot", 10000));
  CHECK(nowMs() >= 700);
  run(3000);
  CHECK(g_log.find("# TCU boot") != std::string::npos);
  CHECK(g_log.find("# cal VSS_PULSES_PER_MILE=") != std::string::npos);
  CHECK(g_log.find("# PBIT") != std::string::npos);
  CHECK(screen.on);
  CHECK(screenSays("STOCK"));
  CHECK(screenSays("TCU IS OFF"));
  CHECK(!OUT(RELAY) && !OUT(S1) && !OUT(S2) && !OUT(SL));
  CHECK(factorySafe());
  CHECK(g_log.find("RESTARTED") == std::string::npos);
}

static void s_not_calibrated_never_takes_over() {
  keyOn();
  startEngine();
  armAndDrive();
  CHECK(runUntilScreen("NOT CALIBRATED", 2000));
  rampTo(45, 10);
  run(2000);
  CHECK(!tcuInControl());
  CHECK(factorySafe());
}

static void s_atf_probe_reads() {
  car.atfC = 90.0f;  // 194 F
  keyOn();
  run(3000);  // a conversion every second
  CHECK(runUntilScreen("194F", 3000));
  car.atfC = 60.0f;  // 140 F
  CHECK(runUntilScreen("140F", 5000));
}

static void s_atf_probe_unplugged() {
  car.probeUnplugged = true;
  keyOn();
  run(5000);
  CHECK(screenSays("NO TEMP"));
  CHECK(screenSays("CHECK THE PLUG"));
}

static void s_screen_cable_cut_and_back() {
  keyOn();
  car.screenUnplugged = true;
  run(5000);
  car.screenUnplugged = false;
  run(4000);  // the firmware re-probes every 2 s and redraws
  CHECK(screenSays("STOCK"));
  uint32_t slow, margin;
  health(slow, margin);
  CHECK(slow > 0 && slow < 50000);  // even with the screen gone, no loop near the watchdog
}

static void s_health_on_the_real_chip() {
  car.atfC = 80;
  keyOn();
  startEngine();
  run(25000);  // two health lines
  uint32_t slow, margin;
  health(slow, margin);
  printf("    real chip: slowest loop %.1f ms, stack margin %u bytes\n", slow / 1000.0, margin);
  CHECK(count(g_log, "# health ") >= 2);
  CHECK(slow > 0 && slow < 50000);
  CHECK(margin >= 1024 && margin < 8192);
}

// The self-test, answered like a person at the bench would, from the screen.
// Ends with the deliberate watchdog reset, through the real bootloader.
static void s_full_self_test_with_safety_reset() {
  car.atfC = 30;
  car.switchOn = true;
  keyOn();
  car.test = true;
  run(2300);  // hold TEST 2 s
  car.test = false;
  run(300);
  CHECK(g_log.find("# IBIT start") != std::string::npos);
  const size_t ibitAt = g_log.find("# IBIT start");
  bool sawRestartScreen = false;
  for (int t = 0; t < 120000 && g_log.find("# TCU boot", ibitAt) == std::string::npos; t += 50) {
    // Between "expecting a reset" and the reset: the screen says what's happening.
    if (g_log.find("expecting a reset", ibitAt) != std::string::npos && screenSays("RESTART NOW") &&
        screenSays("SAFETY RESET TEST:") && screenSays("THE TCU SHOULD") && screenSays("RESTART   |")) {
      sawRestartScreen = true;
    }
    car.up = screenSays("UPSHIFT THE LEVER");
    car.down = screenSays("DOWNSHIFT THE LEVER");
    if (screenSays("TURN ON ONLY ECO")) car.eco = true, car.sport = car.manual = false;
    if (screenSays("TURN ON ONLY SPORT")) car.sport = true, car.eco = car.manual = false;
    if (screenSays("TURN ON ONLY MANUAL")) car.manual = true, car.eco = car.sport = false;
    car.brake = screenSays("PRESS THE BRAKE");
    if (screenSays("TURN ON/OFF OFF")) car.switchOn = false;
    if (g_log.find("expecting a reset", ibitAt) != std::string::npos) {
      run(5);  // the screen is up for well under 250 ms: look often
      continue;
    }
    run(50);
  }
  CHECK(sawRestartScreen);
  CHECK(g_log.find("# IBIT watchdog test: expecting a reset", ibitAt) != std::string::npos);
  const size_t bootAt = g_log.find("# TCU boot", ibitAt);
  CHECK(bootAt != std::string::npos);  // the watchdog really reset the chip
  CHECK(g_resets == 1);
  run(3000);
  const std::string after = bootAt == std::string::npos ? "" : g_log.substr(bootAt);
  // Every check that a bench can settle must say OK; none may say BAD.
  for (const char* item : {"BATTERY", "GAS PEDAL", "TEMP PROBE", "GYRO", "SHIFTER", "SOLENOID 1", "SOLENOID 2",
                           "LOCKUP SOL", "RELAYS", "SHIFT UP", "SHIFT DOWN", "ECO SWITCH", "SPORT SWITCH",
                           "MANUAL SWITCH", "BRAKE", "ON/OFF CUT", "SAFETY RESET"}) {
    const std::string r = bitResult(after, item);
    if (r != "OK") printf("    self-test %s: %s\n", item, r.c_str());
    CHECK(r == "OK");
  }
  CHECK(bitResult(after, "CALIBRATED") == "CHECK");  // as shipped: a reminder, not a fault
  CHECK(after.find(" BAD") == std::string::npos);
  CHECK(after.find("RESTARTED") == std::string::npos);
  CHECK(!screenSays("PROBLEM"));
  // The table came back whole: its amps too, and the screen shows the result.
  CHECK(after.find("SOLENOID 1 OK   1.00A") != std::string::npos);  // 12.6 V into 12.5 ohms
  CHECK(screenSays("ALL OK"));
  // A later reset (the reset button) must not bring the old table back again.
  const size_t again = g_log.size();
  avr_reset(avr);
  CHECK(runUntilLog("# TCU boot", 10000, again));
  run(3000);
  CHECK(bitResult(g_log.substr(again), "SAFETY RESET") == "--");
  CHECK(bitResult(g_log.substr(again), "SOLENOID 1") == "--");
}

// ------------------------------------------------------------------ scenarios: calibrated
// The same build with CALIBRATED = true, as tools/calibrate.py writes it.

static void s_drive_takes_over_and_shifts() {
  car.atfC = 80;
  keyOn();
  startEngine();
  armAndDrive();
  CHECK(tcuInControl());
  const size_t drivingFrom = g_log.size();
  car.tps = 25;
  int highest = 0;
  for (int i = 0; i < 600; i++) {  // 0 -> 60 mph
    car.mph = fminf(60.0f, car.mph + 0.1f);
    run(10);
    if (engagedGear() > highest) highest = engagedGear();
  }
  run(3000);
  CHECK(tcuInControl());
  CHECK(highest == 4);
  CHECK(engagedGear() == 4);
  CHECK(screenSays("[gear 4]"));
  run(12000);
  // Health lines cover the 10 s before them: skip the first one after the
  // take-over (it still covers the boot), keep those that cover only driving.
  uint32_t slow, margin;
  health(slow, margin, g_log.find("# health ", drivingFrom) + 1);
  printf("    real chip, driving: slowest loop %.1f ms, stack margin %u bytes\n", slow / 1000.0, margin);
  CHECK(slow > 0 && slow < 20000);  // the control loop runs every 10 ms: nothing may hold it up for long
}

static void s_manual_lever_shifts() {
  car.eco = false;
  car.manual = true;
  keyOn();
  startEngine();
  armAndDrive();
  CHECK(tcuInControl());
  car.tps = 20;
  rampTo(22, 8);
  CHECK(engagedGear() == 1);  // MANUAL: no upshift by itself
  auto tap = [](bool& lever) {
    lever = true;
    run(150);
    lever = false;
    run(1200);  // the shift, and the 700 ms between shifts
  };
  tap(car.up);
  CHECK(engagedGear() == 2);
  CHECK(screenSays("[gear 2]"));
  tap(car.up);
  CHECK(engagedGear() == 3);
  tap(car.down);
  CHECK(engagedGear() == 2);
  CHECK(tcuInControl());
}

static void s_onoff_off_hands_back() {
  keyOn();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(40, 10);
  CHECK(tcuInControl());
  car.switchOn = false;
  run(100);
  CHECK(!tcuInControl());
  CHECK(factorySafe());
}

static void s_solenoid_wire_breaks() {
  keyOn();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(20, 10);  // 2nd: S2 on
  CHECK(tcuInControl());
  car.solenoidOpen[1] = true;
  run(1000);
  CHECK(!OUT(RELAY));
  CHECK(runUntilScreen("SOLENOID WIRE", 2000));
}

static void s_speed_signal_lost() {
  keyOn();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(45, 10);
  CHECK(tcuInControl());
  car.vssLost = true;
  run(1500);
  CHECK(!OUT(RELAY));
  CHECK(runUntilScreen("NO SPEED SIGNL", 2000));
}

static void s_battery_collapses() {
  keyOn();
  startEngine();
  armAndDrive();
  rampTo(30, 10);
  CHECK(tcuInControl());
  car.battV = 9.0f;
  run(1500);
  CHECK(!OUT(RELAY));
}

static void s_reset_button_mid_drive() {
  keyOn();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(35, 10);
  CHECK(tcuInControl());
  avr_reset(avr);  // the reset line: RAM kept, the bootloader runs
  CHECK(g_resets == 1);
  run(50);
  CHECK(!OUT(RELAY));
  CHECK(factorySafe());
  CHECK(runUntilLog("# TCU boot", 10000, g_log.size()));
  run(3000);
  CHECK(!tcuInControl());  // no take-over without a fresh OFF -> ON
  CHECK(screenSays("WAITING"));
  CHECK(!screenSays("RESTARTED"));
}

static void s_welded_relay_found_at_key_on() {
  car.welded[1] = true;
  car.switchOn = true;  // the key-on relay check needs the coils' power
  keyOn();
  run(2000);
  CHECK(runUntilScreen("RELAY STUCK", 3000));
  car.switchOn = false;
  startEngine();
  armAndDrive();
  rampTo(20, 10);
  CHECK(!OUT(RELAY));  // never takes over with a relay it can't release
}

static void s_tach_lost() {
  keyOn();
  startEngine();
  armAndDrive();
  car.tps = 25;
  rampTo(35, 10);
  CHECK(tcuInControl());
  car.tachLost = true;
  run(4000);
  CHECK(tcuInControl());  // speed x ratio stands in for rpm
  CHECK(runUntilScreen("NO RPM", 3000));
}

// A one-off bad reading, the power-on value, and how often it's read.
static void s_atf_probe_glitches() {
  car.atfC = 60.0f;  // 140 F
  powerOn();
  // From power-on to the first reading, no made-up temperature on the screen.
  for (int t = 0; t < 4000; t += 20) {
    run(20);
    CHECK(!screenSays("32F"));
  }
  // At key-on the first reading is in before the power-on checks.
  const size_t pbit = g_log.find("# PBIT");
  CHECK(pbit != std::string::npos && g_log.find("TEMP PROBE OK", pbit) != std::string::npos);
  CHECK(runUntilScreen("140F", 3000));
  // Read once a second, not more, not less.
  const int c0 = probe.conversions;
  run(10000);
  CHECK(probe.conversions - c0 >= 9 && probe.conversions - c0 <= 11);
  // Two bad readings in a row (noise): still a working probe, no warning.
  car.probeBadReads = 2;
  run(3000);
  CHECK(screenSays("140F"));
  CHECK(!screenSays("NO TEMP"));
  // ...and the count starts over after a good one: two more are fine too.
  car.probeBadReads = 2;
  run(3000);
  CHECK(!screenSays("NO TEMP"));
  // Three in a row: a probe fault.
  car.probeBadReads = 3;
  CHECK(runUntilScreen("NO TEMP", 5000));
  run(3000);  // good again: it comes back by itself
  CHECK(runUntilScreen("140F", 3000));
  // The probe blinks off: its 85.0 C (185 F) power-on value must not be
  // shown, and one such reading isn't a fault.
  for (int i = 0; i < 3; i++) {
    car.probeLostPower = true;
    for (int t = 0; t < 3000; t += 20) {
      run(20);
      CHECK(!screenSays("185F"));
      CHECK(!screenSays("NO TEMP"));
    }
  }
  CHECK(screenSays("140F"));
  // At 85 C for real (185 F, a normal hot day's ATF), the same value is fine.
  car.atfC = 84.0f;
  CHECK(runUntilScreen("183F", 3000));
  car.atfC = 85.0f;
  CHECK(runUntilScreen("185F", 3000));
}

// Corner hold from the real gyro over I2C: its zero (off by 8 deg/s here)
// learned while parked, then a 0.35 g corner holds the gear.
static void s_corner_hold() {
  car.eco = true;
  car.yawDps = 8.0f;
  keyOn();
  startEngine();
  run(4000);
  armAndDrive();
  car.tps = 20;
  rampTo(17, 6);
  run(1000);
  const int g = engagedGear();
  CHECK(!screenSays("CORNER"));
  for (int i = 0; i < 200; i++) {  // speeding up through a 0.35 g corner
    car.mph = fminf(37.0f, car.mph + 0.1f);
    car.yawDps = 8.0f + 0.35f * 9.81f / (car.mph * 0.44704f) * 57.2958f;
    run(10);
  }
  CHECK(engagedGear() == g);
  CHECK(screenSays("CORNER"));
  car.yawDps = 8.0f;  // straight
  run(2500);
  CHECK(!screenSays("CORNER"));
  CHECK(engagedGear() > g);
}

struct Scenario {
  const char* name;
  void (*fn)();
  bool calibrated;
};
static const Scenario kScenarios[] = {
    {"as shipped: boots through the bootloader, safe, screen up", s_boot_through_bootloader, false},
    {"as shipped: not calibrated, never takes over", s_not_calibrated_never_takes_over, false},
    {"as shipped: the DS18B20 probe is read over 1-Wire", s_atf_probe_reads, false},
    {"as shipped: probe unplugged, TRANS --", s_atf_probe_unplugged, false},
    {"as shipped: probe glitches: bad readings, power-on value, once a second", s_atf_probe_glitches, false},
    {"as shipped: screen cable cut and plugged back", s_screen_cable_cut_and_back, false},
    {"as shipped: health on the real chip (loop time, stack)", s_health_on_the_real_chip, false},
    {"as shipped: full self-test, SAFETY RESET through the real bootloader", s_full_self_test_with_safety_reset, false},
    {"calibrated: takes over in D and shifts 1-4", s_drive_takes_over_and_shifts, true},
    {"calibrated: MANUAL, the lever shifts up and down", s_manual_lever_shifts, true},
    {"calibrated: corner hold from the gyro over I2C, its zero learned", s_corner_hold, true},
    {"calibrated: ON/OFF off hands back within 100 ms", s_onoff_off_hands_back, true},
    {"calibrated: solenoid wire breaks, hands back, says so", s_solenoid_wire_breaks, true},
    {"calibrated: speed signal lost, hands back, says so", s_speed_signal_lost, true},
    {"calibrated: battery collapses, hands back", s_battery_collapses, true},
    {"calibrated: reset button mid-drive, no take-over after", s_reset_button_mid_drive, true},
    {"calibrated: welded relay found at key-on, never takes over", s_welded_relay_found_at_key_on, true},
    {"calibrated: tach lost, still in control, NO RPM", s_tach_lost, true},
};

int main(int argc, char** argv) {
  const char* only = getenv("CHIP_ONLY");  // run just the scenarios whose name has this in it
  if (argc < 4) {
    printf("usage: chip_test as-shipped.hex calibrated.hex bootloader.hex\n");
    return 2;
  }
  setvbuf(stdout, nullptr, _IONBF, 0);
  int failed = 0, n = 0;
  for (const Scenario& s : kScenarios) {
    if (only && !strstr(s.name, only)) continue;
    n++;
    const pid_t pid = fork();
    if (pid == 0) {
      g_hex = s.calibrated ? argv[2] : argv[1];
      g_boot = argv[3];
      s.fn();
      if (getenv("CHIP_LOG")) printf("----- USB log -----\n%s\n-------------------\n", g_log.c_str());
      printf("%s %s  (%d checks)\n", g_failures ? "FAIL" : "ok  ", s.name, g_checks);
      fflush(stdout);
      _exit(g_failures ? 1 : 0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      failed++;
      if (!WIFEXITED(status)) printf("FAIL %s  (crashed)\n", s.name);
    }
  }
  printf("\n%d chip scenarios, %d failed\n", n, failed);
  return failed ? 1 : 0;
}
