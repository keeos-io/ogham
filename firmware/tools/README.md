# Ogham Monitor

A PC app that visualizes the running module live over the debugger (ST-Link/SWD):
the 7-segment display (exact segments, even with no TM1637 wired), module state,
pot positions, and CV inputs. For debugging and calibration.

It reads the firmware's `g_telemetry` struct via OpenOCD — **no USB to the
Daisy**, so the module runs on **Eurorack power** with the CV inputs live, and
there's no dual-supply contention.

## Run

1. Power the module from **Eurorack** and connect the **ST-Link** (the same SWD
   setup used for flashing). The board must be powered, or OpenOCD can't connect.
2. Close anything else using the ST-Link (you can't flash while the monitor runs).
3. `python ogham_monitor.py`

The app launches its own OpenOCD instance, finds `g_telemetry` via `nm` on
`build/ogham_bytebeat.elf`, and polls at ~12 Hz.

## Shows
- **7-segment display** — the exact bytes the firmware sends to the TM1637.
- **State** — mode (SELECT/DELAY), edited voice, Out1/Out2 formulas, delay,
  params A/B, rate, ext-clock, lo-fi clean, gate/clock inputs, CPU load.
- **Bars** — raw pot ADCs (A/B/Rate/Level), combined pot+CV (A/B), CV ADC pins
  (shown as volts).

## Notes
- Paths to OpenOCD and `arm-none-eabi-nm` are set at the top of the script.
- The telemetry struct layout is `STRUCT_FMT` in the script; it must match
  `struct Telemetry` in `ogham_main.cpp`. If you change the struct, update both.
- The struct address is found automatically (via `nm`); it moves on rebuilds.

## Other tools here

This README covers `ogham_monitor.py`. The rest, briefly:

- `read_once.py` — one telemetry snapshot instead of a live view.
- `cv_range.py` — sweeps the ENV Out and reports the voltage range reached.
- `bpm_probe.py` — watches the tempo estimator's internal state (confidence,
  period, whether it is locked). Imports `ogham_monitor`, so the same OpenOCD and
  `arm-none-eabi-nm` paths apply.
- `voct_range.cpp`, `voct_tone.cpp` — offline analysis of the shipped bank under
  V/oct: which formulas sound at a given pitch, and what register each one lands
  in. They compile against `../src/formulas.h` alone, so they need no hardware:

      c++ -std=c++17 -O2 -I../src voct_range.cpp -o voct_range

