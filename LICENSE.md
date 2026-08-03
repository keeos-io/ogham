# Licensing

Ogham is split across two licences, because software and hardware are different
kinds of thing and the usual licences for each do not translate well.

| What | Licence | File |
|---|---|---|
| Firmware — everything under `firmware/` | **MIT** | [`LICENSE-firmware.txt`](LICENSE-firmware.txt) |
| Hardware — everything under `hardware/`: schematics, PCB, panel, gerbers, BOM | **CC BY-SA 4.0** | [`LICENSE-hardware.txt`](LICENSE-hardware.txt) |
| Documentation — `docs/`, and the READMEs in this repository | **CC BY-SA 4.0** | [`LICENSE-hardware.txt`](LICENSE-hardware.txt) |

Copyright © 2026 Steven Collins (Keeos).

## In short

- **Build one.** Order the boards, populate them, flash the firmware. No permission
  needed, and that includes building one to sell.
- **Change it.** Fork the firmware, redraw the board, rework the panel.
- **Share your changes to the hardware.** CC BY-SA is share-alike: if you
  distribute a modified board — or a product based on it — the modified design
  files go out under the same licence. The firmware is MIT and carries no such
  obligation.
- **Credit the original.** Both licences require attribution.

This summary is for orientation only. The licence files are what actually
applies.

## What is *not* covered

**The name "Keeos", the name "Ogham" as used for this module, and the panel
artwork as a brand.** The panel *design files* are under CC BY-SA and you may use
and adapt them — but please do not present a board you have built or modified as
being a Keeos product, and do not use the Keeos name or wordmark to promote it.
Rename your version. This is the usual open-hardware arrangement: the design is
open, the identity is not.

The panel typeface is **Uncial Antiqua**, licensed separately under the SIL Open
Font License — see [`THIRD-PARTY.md`](THIRD-PARTY.md).

## Third-party components

The firmware builds against libDaisy and DaisySP, both MIT-licensed by
Electrosmith and included here as pinned git submodules rather than copies. See
[`THIRD-PARTY.md`](THIRD-PARTY.md) for the full list and their notices.

## No warranty

Both licences disclaim warranty in full, and that is meant literally. This is a
prototype-stage design published so others can learn from and build on it. It has
been built and tested on a handful of units, not qualified for production. You
are responsible for what you build: check the design yourself, and take the usual
care with a module that connects to a ±12 V supply.
