# Ogham hardware

KiCad 10 sources and a ready-to-upload fabrication package for the shipping
board revision, **ogham-merged-v1.0**.

| | |
|---|---|
| Main PCB | 50 × 110 mm, 2-layer, double-sided SMT |
| Panel | 10 HP (50.5 × 128.5 mm) |
| Depth behind panel | ~32 mm including power-cable clearance — skiff-friendly |
| Supply | Eurorack ±12 V, with an on-board +5 V regulator for the Daisy Seed |
| MCU | Electro-Smith Daisy Seed (STM32H750), on pin-header standoffs |

Only this revision is published. Earlier prototypes (`ogham-logic`, `ogham-ui`
and their v0.2 iterations) are not included, so there is nothing here to
fabricate by mistake.

## Layout

| Path | Contents |
|---|---|
| `ogham-merged-v1.0/` | Schematics (hierarchical: power, MCU, CV I/O, audio, UI), PCB, netlist |

The schematic is a two-level hierarchy: `ogham-merged-v1.0` is the root, and it
pulls in `ogham-logic-v2.0` (→ power, MCU, CV I/O) and `ogham-ui-v0.2` (→ audio,
UI). Those two keep the sheet names they had when the design was two separate
boards — they are **not** old revisions, they are live sub-sheets of this one.

| `panel/` | Panel artwork SVGs, and `PanelPCB/` — the panel as a fabricable PCB |
| `production/` | Gerbers, BOM, placement, IPC netlist, and the fab checklist |

## Having the boards made

The package in `production/` is formatted for JLCPCB assembly, but the gerbers
are standard and will work anywhere. **Read
[`production/JLC-upload-checklist.md`](production/JLC-upload-checklist.md)
before ordering** — it covers the whole flow, including the two things most
likely to cost you a board run.

### ⚠ 1. Nineteen parts must be marked "Do Not Place"

90 SMD parts are machine-assembled. **19 designators across 14 BOM rows are
hand-soldered** and will be flagged by the fab as unresolvable, because they
carry no LCSC number. Mark every one of them DNP:

- **C5** — 10 µF tantalum (`TAP106J016SCS`). This one is easy to miss: it looks
  like an ordinary SMD part in the BOM and it is the **+5 V bulk capacitor**, so
  a board that quietly ships without it is not a board you want.
- **A1** — the Daisy Seed
- **J1** — Eurorack power header
- **8 × jacks** — `J_CLK1`, `J_CVA1`, `J_CVB1`, `J_GATE_IN1`, `J_CV_OUT1`,
  `J_GATE_OUT1`, `J_OUT1`, `J_OUT2`
- **J_DISP1**, **J_DISP_SUPP** — display headers
- **4 × pots** — `RVA1`, `RVB1`, `RVLEVEL1`, `RVRATE1`
- **SW_ENC1** — encoder · **SW_MODE1** — mode toggle

### ⚠ 2. Check rotations before you confirm

Rotation is the classic assembly error — KiCad and the fab disagree about
reference orientation. Step through the placement preview and check the
polarised parts: diode cathodes (D1, D2, D8, D3, D4, D5, D_OCT1, D_OCT2, D6,
D7), IC pin-1 (U1–U7), and Q1's collector/emitter. Assembly is **double-sided**,
so also confirm the top-layer parts really are on top.

## Hand-populated parts

Sources that have worked (UK):

| Part | Qty | Where |
|---|---|---|
| Thonkiconn PJ398SM jacks | 8 | [thonk.co.uk](https://www.thonk.co.uk/shop/thonkiconn/) |
| Alpha 9 mm B10K D-shaft pots | 4 | [thonk.co.uk](https://www.thonk.co.uk/shop/alpha-9mm-pots-dshaft/) |
| Rotary encoder, 20 mm T18 shaft, 24 PPR with switch | 1 | [thonk.co.uk](https://www.thonk.co.uk/shop/20mm-encoder-pots-t18/) |
| Breakaway pin header | 1 bag | [thonk.co.uk](https://www.thonk.co.uk/shop/60-pin-header/) |
| 2×5 IDC power header | 1 | Mouser / Soundtronics / York Modular |
| Daisy Seed (Rev 4) | 1 | Electro-Smith |
| C5, 10 µF tantalum | 1 | Any distributor — `TAP106J016SCS` or equivalent |

> **Encoder note.** Two batches exist with opposite A/B phase, and the firmware
> has a build for each. Whichever you fit, if the Func encoder counts down when
> turned clockwise, flash the other build — see
> [`../firmware/README.md`](../firmware/README.md).

## Panel

`panel/` holds the artwork as SVG and as a fabricable PCB (`PanelPCB/`), which is
how the panels were actually made — a PCB panel is cheap, flat, and takes the
graphics as silkscreen.

**The fabrication files do not depend on any fonts.** The artwork is stored as
polygons, converted from the SVG, so a panel ordered from `PanelPCB/production/`
comes out right regardless of what is installed on your machine.

**Editing the SVG does need fonts** — including Bahnschrift, which is a Windows
system font and cannot be redistributed. See
[`../THIRD-PARTY.md`](../THIRD-PARTY.md) for the full list and what to do on
macOS or Linux.

## Known issues in this revision

Carried here so you can fix them in a fork rather than rediscover them:

- **Audio output runs ~2× hot.** The TL072 stage is designed at 4.7×, giving
  ~19.5 Vpp full-scale against the ~10 Vpp Eurorack norm. The firmware
  compensates digitally (`AUDIO_OUT_LEVEL = 0.51`); halving the analog gain would
  be the real fix.
- **CV A / CV B are unipolar** in front-end terms and clamp negative input; a
  bipolar ±5 V front end is the intended next revision.
- **The V/oct input is unipolar** (0–5 V), so negative CV floors at the base
  note.
- **Jacks have no switch/normalling contacts wired to GPIO**, so the firmware
  cannot detect a cable being unplugged — it can only infer it from signals
  stopping.
- **The panel legend reads "ENV OUT"** where "CV OUT" would describe the jack
  better, since it can output a raw bytebeat as DC as well as an envelope.

## Design notes

`ogham_pins.h` in the firmware is the pin-assignment source of truth and matches
this schematic. If you rework the board, keep them in step.

Two constraints worth knowing before you move things: the TM1637 display sits on
**D9/D10 (PB4/PB5)**, which are SPI pins with **no I²C alternate function** — an
I²C peripheral needs different pins. Seed pins 12/13 (PB8/PB9, I²C1) are
unconnected and are the obvious place to go.
