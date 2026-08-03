# Ogham firmware

C++ for the STM32H750 on an Electro-Smith Daisy Seed, built against libDaisy and
DaisySP. Runs from **internal flash at `0x08000000`** — there is no bootloader
involved, so the whole 128 KB region is the application.

## Toolchain

- **Arm GNU Toolchain 14.2.Rel1** (`arm-none-eabi-gcc`) — a different version
  will build fine but will not reproduce the published checksums
- GNU Make
- For flashing: **OpenOCD** with an ST-Link, or **dfu-util** over USB

## Building

From a fresh clone, libraries first:

```bash
git clone --recurse-submodules https://github.com/stevec64/keeos-ogham.git
cd keeos-ogham
make -C lib/libDaisy -j8
make -C lib/DaisySP  -j8
cd firmware && make -j8            # -> build/ogham_bytebeat.{elf,bin,hex}
```

If you cloned without `--recurse-submodules`, fix it with
`git submodule update --init --recursive`. libDaisy has nested submodules
(CMSIS, the STM32 HAL); without them the build fails part-way with
`No rule to make target '.../stm32h7xx_hal.o'`.

Libraries elsewhere? Override the path:

```bash
make LIB_DIR=/path/to/libs -j8     # expects $LIB_DIR/libDaisy and $LIB_DIR/DaisySP
```

## ⚠ Two encoder builds — pick the right one

Two batches of rotary encoder exist with **opposite A/B phase**. The firmware
cannot detect which is fitted, so there are two builds:

| Build | Command | Output | For |
|---|---|---|---|
| **Default** | `make -j8` | `build/ogham_bytebeat.bin` | The current encoder — **almost certainly what you want** |
| Original | `make -f Makefile.encorig BUILD_DIR=build_encorig -j8` | `build_encorig/ogham_bytebeat_encorig.bin` | Early units with the original part |

**If after flashing the Func encoder counts *down* when you turn it clockwise,
you flashed the wrong one.** Flash the other and it is fixed. Nothing else
differs between them — same version, same behaviour, same display.

> Note for anyone reading older release notes: this naming **flipped in v1.05**.
> The default build used to be the original encoder, with the reversed one as
> the variant. It is now the other way round, because the reversed part is what
> current modules use.

## Flashing

**ST-Link (SWD)** — no button-holding needed:

```bash
make program        # openocd: program … verify reset exit
```

Look for `** Verified OK **`. `Target voltage: 0.00V` means the rack is off or
the SWD header is not seated; `~1.8V` where you expect ~3.3V is a loose contact.

**USB DFU** — hold `BOOT`, tap `RESET`, release `BOOT`, then:

```bash
dfu-util -a 0 -s 0x08000000:leave -D build/ogham_bytebeat.bin
```

The `:leave` suffix matters. Without it the Seed stays parked in DFU mode with a
blank display, looking dead. A trailing `Error during download get_status` is a
benign detach artefact — the write still succeeded.

**After flashing, unplug USB before power-cycling.** The Seed is powered by USB
*and* by the Eurorack bus, so with USB connected, switching the case off does not
actually reset the chip — the new firmware never starts and the module looks
broken when it is fine.

## Layout

| File | Role |
|---|---|
| `ogham_main.cpp` | Init, main loop, encoder/menu state machine, persistence, telemetry |
| `bytebeat_engine.*` | 32.32 fixed-point phase accumulator; dual voice; hard sync; drone |
| `formulas.*` | The formula bank as inline functions, plus metadata |
| `ogham_audio_pipeline.*` | Tone macro, FX chain, low-pass gate |
| `ogham_controls.*` | ADC smoothing, quadrature decode, mode switch |
| `ogham_display.*`, `tm1637.*` | 4-digit 7-segment display |
| `ogham_cv_output.*` | Envelope follower / DC out on the DAC |
| `bpm_clock.*` | Tempo estimation for the EOC output |
| `ogham_pins.h` | Pin assignments — the source of truth, matching the schematic |

Formulas are compiled in. To add one: write the inline function in `formulas.h`,
add its `FormulaInfo` entry in `formulas.cpp`, rebuild. All bitwise operations
follow JavaScript `ToInt32` semantics (shift counts masked to `& 31`, output
masked to `& 0xFF`) so formulas behave as they do in browser bytebeat tools.

Appendix A of the manual lists every formula's expression and what A and B change.

## Calibration

Pots are non-linear because of the 10 kΩ pull-down, so the firmware holds
measured 12-o'clock ADC values for the Rate and Tone knobs. Those constants are a
fleet mean; if your build's centre detent feels off, measure yours:

```bash
make -f Makefile.diag -j8       # -> build_diag/ogham_pot_diag.{elf,bin}
```

Flash it, read the values off the display, and update `RATE_POT_CENTER` in
`ogham_main.cpp` and `LOFI_CENTER` in `ogham_audio_pipeline.cpp`.

`tools/` holds Python scripts that read live telemetry over SWD while the module
runs — useful for calibration and for watching CPU load.
