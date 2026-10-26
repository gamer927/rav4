# 1999 RAV4 Transmission Controller

Standalone transmission controller for a 1999 Toyota RAV4 (first generation, 4-speed ECT
automatic, 3S-FE). It adds ECO/NORMAL/SPORT shift and lockup maps, manual [+]/[−] shifting and an
aircraft-style self-test with a small console screen. The factory ECU stays wired in as the
fail-safe fallback: switch it OFF, or let anything go wrong, and the car is stock.

**Console:** ON/OFF switch · ECO switch · SPORT switch · [+] · [−] · TEST button · status LED,
plus a 0.96"/1.3" OLED in the dash.

| Document | Contents |
|---|---|
| [`docs/01-audit.md`](docs/01-audit.md) | Review of the original (rev 0) spec: 6 critical, 9 high, 7 medium and 2 low findings, each with its fix. Historical: rev 0 still had the oil sender and Nextion |
| [`docs/02-revised-spec.md`](docs/02-revised-spec.md) | Revised design: hardware, wiring, pin map, solenoid logic, control rules, calibration, test plan |

## Layout

```
firmware/tcu/tcu.ino        hardware layer: pins, ISRs, ADC, relays, watchdog, OLED pages, CSV log
firmware/tcu/tcu_logic.*    all control decisions (no Arduino deps, unit-tested on the host)
firmware/tcu/self_test.*    built-in test: power-up, continuous and press-to-test (unit-tested)
firmware/tcu/oled.*         non-blocking console OLED driver (SSD1306 / SH1106)
firmware/tcu/config.h       calibration & tuning constants (VERIFY / CALIBRATE before use)
test/                       host unit tests
tools/avr_build.sh          compile-check for ATmega2560 without the Arduino IDE
```

## Build

```sh
make test   # host unit tests (g++)
make avr    # ATmega2560 build (needs: apt install gcc-avr avr-libc arduino-core-avr)
```

To flash, open `firmware/tcu/tcu.ino` in the Arduino IDE (board: Arduino Mega 2560), or run
`pio run -t upload` with PlatformIO.

## Before it touches a car

1. Read spec §0 (what is confirmed for the 1999 RAV4), then confirm every **VERIFY** item in
   spec §10 against the factory repair manual and wiring diagram for your VIN.
2. Calibrate in **STOCK** mode first (spec §8). The over-rev guard is only as accurate as
   `RPM_PER_KMH` and `VSS_PULSES_PER_KM`, so CUSTOM stays locked out until you set
   `CALIBRATED = true` in `config.h`.
3. Hold the console TEST button for 2 s with the car parked and the engine off to run the
   built-in self-test (spec §7). [`docs/oled-screens.png`](docs/oled-screens.png) shows what
   each screen looks like.
4. Follow the bring-up plan in spec §9: bench, then static, then lift (all four wheels up),
   then a closed lot, then the road.

This modifies drivetrain control. Test off public roads, and check your local regulations and
insurance.
