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
| `panel/` | Panel artwork SVGs, and the panel as a fabricable PCB — `PanelPCB-v2/` is current |
| `production/` | Gerbers, BOM, placement, IPC netlist, and the interactive BOM |

The schematic is a two-level hierarchy: `ogham-merged-v1.0` is the root, and it
pulls in `ogham-logic-v2.0` (→ power, MCU, CV I/O) and `ogham-ui-v0.2` (→ audio,
UI). Those two keep the sheet names they had when the design was two separate
boards — they are **not** old revisions, they are live sub-sheets of this one.

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

## Interactive BOM — use this while assembling

[`production/ogham-merged-v1.0-ibom.html`](production/ogham-merged-v1.0-ibom.html)
is a single self-contained file: click a line in the BOM and those parts light up
on the board, front and back, with checkboxes for tracking what you have sourced
and placed. No install, no network — **download it and open it in a browser**.

> GitHub displays committed HTML as source rather than rendering it, so use the
> download button rather than clicking through — the file is useless as text and
> perfectly good once saved.

It carries an **LCSC** column, which doubles as the hand-populate list: a part
with an LCSC number is machine-assembled, and a part **without** one is yours to
solder. `C5` shows its bare MPN `TAP106J016SCS`, and the jacks, pots, encoder and
headers show nothing at all — those are exactly the 19 designators to mark Do Not
Place.

Regenerate it with [`gen-ibom.sh`](gen-ibom.sh) whenever the board changes; the
output is committed, so it can otherwise drift from the design. It needs KiCad's
own Python and [InteractiveHtmlBom](https://github.com/openscopeproject/InteractiveHtmlBom)
**v2.11.2 or newer** — earlier versions cannot read KiCad 10 files.

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

`panel/` holds the artwork as SVG and as a fabricable PCB, which is how the
panels were actually made — a PCB panel is cheap, flat, and takes the graphics as
silkscreen.

**`PanelPCB-v2/` and `Ogham Panel Graphics (JLCPCB) v2.svg` are the current
revision**, and the one the built modules use. `PanelPCB/` and the unversioned
SVG are the first revision, kept because the earliest modules were made from
them. Order from v2.

**The fabrication files do not depend on any fonts.** The artwork is stored as
polygons, converted from the SVG, so a panel ordered from
`PanelPCB-v2/production/` comes out right regardless of what is installed on your
machine.

**Editing the SVG does need fonts**, though all three are now free and
redistributable — the v2 artwork replaced Bahnschrift, a Windows system font,
with DINish. See
[`../THIRD-PARTY.md`](../THIRD-PARTY.md) for the full list and what to do on
macOS or Linux.

## Known issues and planned changes

Carried here so you can fix them in a fork rather than rediscover them, and so
you can see what is likely to move in the next revision before you commit to a
fab run.

### Electrical

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

### Mechanical

- **The mounting holes are slightly out of position.** The module still fits most
  cases, but it is more of a stretch than it should be. They move in a panel
  update.
- **The bottom row of jacks needs to rise by about 1 mm.** The current clearance
  is enough in some cases and not in others, which was discovered the hard way
  rather than by measurement. Fixed in the next revision.
- **The pot and jack holes in the panel are piecewise curves**, not drill holes —
  an artefact of how the artwork was converted. They will become real drill holes
  in the PCB, which is both more precise and easier to fab.
- **The seven-segment display sits poorly behind the panel.** It works, but it is
  not a finished-looking job. The plan for v1.1 is a transparent acrylic window,
  which also protects the display. The display itself is an Amazon-sourced part
  and is likely to be replaced eventually — suitable alternatives are
  surprisingly hard to find, so it stays for now.

### Parts and sourcing

- **C5 is a through-hole tantalum** for stock reasons, not design ones: a supply
  was bought before a good SMD equivalent that JLCPCB could place turned up. It
  is one of the hand-soldered parts as a result, and is likely to become SMD.
- **Two encoder batches, two firmware builds.** Two parts with opposite A/B phase
  have been prototyped and neither has been settled on, which is why the firmware
  ships in default and original-encoder variants. Standardising on one part in
  the next revision removes the variant build.
- **The recent prototypes were built with centre-detent pots**, which was a
  mistake in ordering rather than a design choice. A production run without them
  may need the pot calibration re-tuned;
  [`firmware/tools/cv_range.py`](../firmware/tools/cv_range.py) is the utility
  for that.

### Panel artwork and labelling

Both of the issues that used to be listed here are fixed in the v2 artwork, and
are recorded because the first panels were made before it:

- **The label font was Bahnschrift**, a Microsoft system font that cannot be
  redistributed and is not available on macOS or Linux. v2 uses DINish, which is
  OFL — so every typeface on the panel is now free. See
  [`../THIRD-PARTY.md`](../THIRD-PARTY.md).
- **The legend read "ENV OUT"** where "CV Out" describes the jack better: it
  outputs a raw bytebeat as DC as well as an envelope follower. v2 reads
  "CV Out".

## Design notes

`ogham_pins.h` in the firmware is the pin-assignment source of truth and matches
this schematic. If you rework the board, keep them in step.

Two constraints worth knowing before you move things: the TM1637 display sits on
**D9/D10 (PB4/PB5)**, which are SPI pins with **no I²C alternate function** — an
I²C peripheral needs different pins. Seed pins 12/13 (PB8/PB9, I²C1) are
unconnected and are the obvious place to go.
