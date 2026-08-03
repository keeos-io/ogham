# Ogham

A dual-voice **bytebeat synthesizer** for Eurorack, built on the Electro-Smith
Daisy Seed. This repository holds everything needed to build one: firmware
source, schematics, PCB, panel, and the fabrication package.

Instead of oscillators and wavetables, Ogham runs *bytebeat* expressions —
single lines of integer arithmetic that, evaluated over time, produce
surprisingly rich rhythmic and melodic patterns. It hosts a bank of them, plays
two at once, and clocks, pitches and modulates them from the rest of your rack.

- **Manual:** <https://keeos.co.uk/docs/ogham-manual/> (or [`docs/ogham-manual.pdf`](docs/ogham-manual.pdf))
- **Firmware downloads and changelog:** <https://keeos.co.uk/firmware/ogham/>
- **Module overview:** <https://keeos.co.uk/modules/ogham/>

> **Status: prototype.** Built and tested on a handful of units, not qualified
> for production. Published so others can learn from it and build on it. Check
> the design yourself before you commit money to a fab run.

## What's here

| Path | Contents |
|---|---|
| [`firmware/`](firmware/) | Firmware source, build and flashing instructions |
| [`hardware/ogham-merged-v1.0/`](hardware/ogham-merged-v1.0/) | KiCad schematics and PCB — the shipping revision |
| [`hardware/panel/`](hardware/panel/) | Front panel: artwork, and the panel PCB project |
| [`hardware/production/`](hardware/production/) | Gerbers, BOM, placement — ready to upload to a fab |
| [`docs/`](docs/) | The manual, as a PDF |
| `lib/` | libDaisy and DaisySP, as pinned submodules |

Only the **current** board revision is published. Earlier prototypes are not
included, so there is nothing here to fabricate by mistake.

## Quick start

**To build the firmware** — see [`firmware/README.md`](firmware/README.md) for
the toolchain and the two encoder variants. In short:

```bash
git clone --recurse-submodules https://github.com/stevec64/keeos-ogham.git
cd keeos-ogham && make -C lib/libDaisy -j8 && make -C lib/DaisySP -j8
cd firmware && make -j8
```

`--recurse-submodules` is not optional: libDaisy has nested submodules of its
own, and without them the build fails part-way with a missing HAL object.

**To have the boards made** — see [`hardware/README.md`](hardware/README.md).
Upload `hardware/production/ogham-merged-v1.0.zip`, and **read the
hand-populate list first**: several parts are deliberately excluded from
assembly and a board built from the BOM alone will be missing them.

## Licensing

Firmware is **MIT**. Hardware and documentation are **CC BY-SA 4.0**. Build one,
sell one, fork it — but share hardware modifications alike, and give credit. The
*Keeos* name and the panel identity are not covered.

Full terms in [`LICENSE.md`](LICENSE.md); third-party components and the panel
typefaces in [`THIRD-PARTY.md`](THIRD-PARTY.md).

## A note on reproducible builds

The published binaries are byte-for-byte reproducible from this repository.
Building the firmware at a given tag, with the submodules at their pinned
commits and **Arm GNU Toolchain 14.2.Rel1**, reproduces the exact `.bin` shipped
for that release — verified, not assumed.

Two things will break that: building against a *different* libDaisy commit, or a
different compiler version. Both produce working firmware; neither reproduces the
published checksums. If you are verifying a download rather than developing,
compare against the SHA-256 published with each release.
