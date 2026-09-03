# Ogham hardware

KiCad 10 sources and a ready-to-upload fabrication package for the shipping
board revision, **ogham-merged-v1.0**.

| | |
|---|---|
| Main PCB | 50 × 110 mm, 2-layer, double-sided SMT |
| Panel | 10 HP (50.5 × 128.5 mm) |
| Depth behind panel | ~32 mm including power-cable clearance — skiff-friendly |
| Supply | Eurorack ±12 V; the Seed runs from +12 V through a series Schottky, and an on-board +5 V regulator feeds the display and logic |
| MCU | Electro-Smith Daisy Seed (STM32H750), on pin-header standoffs |

Only this revision is published. Earlier prototypes (`ogham-logic`, `ogham-ui`
and their v0.2 iterations) are not included, so there is nothing here to
fabricate by mistake.

## Layout

| Path | Contents |
|---|---|
| `ogham-merged-v1.0/` | Schematics (hierarchical: power, MCU, CV I/O, audio, UI), PCB, netlist |
| `panel/` | Panel artwork as SVG, and the panel as a fabricable PCB (`PanelPCB-v2/`) |
| `production/` | Gerbers, BOM, placement, IPC netlist, and the interactive BOM |

The schematic is a two-level hierarchy: `ogham-merged-v1.0` is the root, and it
pulls in `ogham-logic-v2.0` (→ power, MCU, CV I/O) and `ogham-ui-v0.2` (→ audio,
UI). Those two keep the sheet names they had when the design was two separate
boards — they are **not** old revisions, they are live sub-sheets of this one.

## Browse the design without installing anything

[KiCanvas](https://kicanvas.org) renders KiCad files in the browser, straight
from this repository — pan, zoom, click a symbol, follow a net. Nothing to
install, nothing to download.

| | |
|---|---|
| Schematic and PCB | [Open in KiCanvas](https://kicanvas.org/?github=https://github.com/keeos-io/ogham/tree/main/hardware/ogham-merged-v1.0) |
| Panel PCB | [Open in KiCanvas](https://kicanvas.org/?github=https://github.com/keeos-io/ogham/tree/main/hardware/panel/PanelPCB-v2) |

Both links point at a **directory**, not at a file, and that is deliberate. Given
a single `.kicad_sch` KiCanvas loads it alone, which for a hierarchical design
means the root sheet and nothing below it — you would get a page of sheet symbols
with no way into them. Pointed at the directory it enumerates every file, so all
nine sheets resolve and the board is in the same viewer, behind the file picker
at the top left.

This matters more than convenience: these are **KiCad 10** sources, and KiCad 9
cannot open them at all. Until you have KiCad 10 in front of you, the links above
are the only way to read the circuit.

KiCanvas is a third-party viewer and fetches from GitHub anonymously, so it is
subject to GitHub's rate limits and is nobody's idea of a permanent archive. The
files in this repository are the source of truth; the links are a convenience.

## Having the boards made

The package in `production/` is formatted for JLCPCB assembly, but the gerbers
are standard and will work anywhere. **Read
[`production/JLC-upload-checklist.md`](production/JLC-upload-checklist.md)
before ordering** — it covers the whole flow, including the two things most
likely to cost you a board run.

### ⚠ 1. Nineteen parts must be marked "Do Not Place"

90 SMD parts are machine-assembled. **19 designators across 13 BOM rows are
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
| Rotary encoder, Bourns `PEC11R-4115K-S0018` | 1 | Any distributor — Mouser, Farnell, Digi-Key |
| Breakaway pin header | 1 bag | [thonk.co.uk](https://www.thonk.co.uk/shop/60-pin-header/) |
| 2×5 IDC power header | 1 | Mouser / Soundtronics / York Modular |
| Daisy Seed (Rev 4) | 1 | Electro-Smith |
| C5, 10 µF tantalum | 1 | Any distributor — `TAP106J016SCS` or equivalent |

> **Encoder note.** `PEC11R-4115K-S0018` is a 15 mm knurled (T18) shaft with a
> push switch, 18 detents and 18 pulses per revolution — one pulse per detent.
> It is the swapped-A/B part, which the **default** firmware build targets. Two
> batches exist with opposite A/B phase and the firmware cannot tell them apart,
> so if the Func encoder counts down when turned clockwise, flash the other
> build — see [`../firmware/README.md`](../firmware/README.md).

## Panel

`panel/` holds the artwork as SVG and as a fabricable PCB, which is how the
panels were actually made — a PCB panel is cheap, flat, and takes the graphics as
silkscreen.

`Ogham Panel Graphics (JLCPCB) v2.svg` is the editable artwork, and
`PanelPCB-v2/` is the KiCad project converted from it — that project, and the
gerbers in its `production/`, are what the built modules use. The first
revision has been removed to leave only one panel to order from; it is still in
the git history if you want it.

**The fabrication files do not depend on any fonts.** The artwork is stored as
polygons, converted from the SVG, so a panel ordered from
`PanelPCB-v2/production/` comes out right regardless of what is installed on your
machine.

**Editing the SVG does need fonts**, though all three are now free and
redistributable — the v2 artwork replaced Bahnschrift, a Windows system font,
with DINish. See
[`../THIRD-PARTY.md`](../THIRD-PARTY.md) for the full list and what to do on
macOS or Linux.

## Design notes

`ogham_pins.h` in the firmware is the pin-assignment source of truth and matches
this schematic. If you rework the board, keep them in step.

Two constraints worth knowing before you move things: the TM1637 display sits on
**D9/D10 (PB4/PB5)**, which are SPI pins with **no I²C alternate function** — an
I²C peripheral needs different pins. Seed pins 12/13 (PB8/PB9, I²C1) are
unconnected and are the obvious place to go.
