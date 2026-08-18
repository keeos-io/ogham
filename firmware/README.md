# Ogham firmware

C++ for the STM32H750 on an Electro-Smith Daisy Seed, built against libDaisy and
DaisySP. Runs from **internal flash at `0x08000000`** — there is no bootloader
involved, so the whole 128 KB region is the application.

## Building

Full per-platform setup for **Windows, macOS and Linux** — toolchain, Make, USB
drivers, udev rules — is in [`../docs/BUILDING.md`](../docs/BUILDING.md). That is
the authority; this file covers only what is specific to the firmware itself.

The short version, once the tools are installed:

```bash
git clone --recurse-submodules https://github.com/keeos-io/ogham.git
cd keeos-ogham
make -C lib/libDaisy -j8 && make -C lib/DaisySP -j8
cd firmware && make -j8            # -> build/ogham_bytebeat.{elf,bin,hex}
```

`--recurse-submodules` is not optional — libDaisy has nested submodules, and
without them the build fails part-way with a missing HAL object. Fix an existing
clone with `git submodule update --init --recursive`.

Libraries elsewhere? `make LIB_DIR=/path/to/libs -j8`.

Reproducing a published binary exactly needs **Arm GNU Toolchain 14.2.Rel1** and
the submodules at their pinned commits; any other compiler builds working
firmware that simply will not match the release checksum.

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

Commands and platform notes are in [`../docs/BUILDING.md`](../docs/BUILDING.md#flashing).
In brief: `make program` with an ST-Link, or hold **BOOT**, tap **RESET**, and

```bash
dfu-util -a 0 -s 0x08000000:leave -D build/ogham_bytebeat.bin
```

The `:leave` matters, and **unplug USB before power-cycling** — the Seed is
powered by USB as well as the rack, so the case switch alone does not reset it.

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

Formulas are compiled in. All bitwise operations follow JavaScript `ToInt32`
semantics (shift counts masked to `& 31`, output masked to `& 0xFF`) so formulas
behave as they do in browser bytebeat tools.

`formulas.h` and `formulas.cpp` are **generated**, not hand-written — the bank is
translated out of the curated library by `tools/gen_ogham_bank.cpp` in the
bytebeat repository (the search and curation tooling, which is not published).
The generator emits the manual's Appendix A from the same manifest, and compares
every generated function against the evaluator the formulas were auditioned on.
Change the library and regenerate rather than editing these two files, or the
module and the manual drift apart.

Which formula lands in which slot is stored on the curated formula itself rather
than derived from its position in the library: the generator reads those numbers
and does not sort, group or renumber. Slots are assigned in the bytebeat app's
CURATE view, which can also preview the whole bank against the firmware's
current `formulas.h` before anything is generated. The end-to-end process —
including the flash-size check, which is the binding constraint on this 128 KB
region — is `docs/PUBLISHING-THE-BANK.md` in that repository.

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
