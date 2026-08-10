# Third-party components

Everything Ogham depends on that was not written for it, and the licence each
arrives under.

## Firmware

| Component | Licence | Source |
|---|---|---|
| **libDaisy** | MIT © Electrosmith, Corp. | <https://github.com/electro-smith/libDaisy> |
| **DaisySP** | MIT © Electrosmith, Corp. | <https://github.com/electro-smith/DaisySP> |

Both are **git submodules pinned to exact commits**, not copies:

| Submodule | Pinned commit |
|---|---|
| `lib/libDaisy` | `f044cdc312455f174b01bef98d9e97598d94d3bc` |
| `lib/DaisySP`  | `599511b740f8f3a9b8db72a0642aa45b8a23c3a3` |

Pinning is deliberate. A firmware binary is only reproducible if the libraries
are, and libDaisy in particular changes underneath you. Building at these commits
reproduces the published binaries byte for byte; building at `main` may not.

Neither library is modified. If you need a newer one, update the submodule and
rebuild — but expect to re-verify, not to match the published checksums.

The hardware itself is built around the **Electro-Smith Daisy Seed**, which is
itself open hardware. Its schematics and design files are Electrosmith's, are not
reproduced here, and are published at <https://github.com/electro-smith/Hardware>.

## Panel typefaces

The panel artwork uses three typefaces. **No font files are redistributed here** —
the SVG sources reference them by name.

| Typeface | Used for | Licence |
|---|---|---|
| **Uncial Antiqua** | the "ogham" wordmark | SIL Open Font License 1.1 — <https://fonts.google.com/specimen/Uncial+Antiqua> |
| **Noto Sans Ogham** | the ogham script characters | SIL Open Font License 1.1 — <https://fonts.google.com/noto/specimen/Noto+Sans+Ogham> |
| **Bahnschrift** | control labels | **Proprietary — Microsoft.** Ships with Windows 10 and later. Not redistributable, and not available on macOS or Linux. |

### What this means in practice

**The fabrication files do not depend on any of them.** `hardware/panel/PanelPCB`
and the gerbers in its `production/` directory carry the artwork as **polygons**,
converted from the SVG by svg2shenzhen. The only text left in the footprint is
KiCad's own `Ref**`/`Val**` placeholders, which do not print. You can order a
panel with no fonts installed at all, on any operating system, and get exactly
the intended result.

**The editable SVG sources do depend on them.** Open
`hardware/panel/*.svg` without these fonts installed and the renderer will
substitute — the lettering will still appear, but with different metrics, so it
will not match the fabricated panel and may overlap its own layout. If you intend
to *edit* the panel rather than just fabricate it:

- Install Uncial Antiqua and Noto Sans Ogham (both free, both OFL, both on Google
  Fonts).
- **Bahnschrift is the awkward one.** It is a Windows system font and cannot be
  redistributed. On macOS or Linux you will need to substitute it — DIN 1451 and
  its descendants are the closest free equivalents — and re-lay-out the labels,
  or work from the polygon version instead. Replacing it outright is a reasonable
  choice for a fork, and avoids the issue entirely.

## Fabrication

The `hardware/production/` package is formatted for JLCPCB's assembly service.
Nothing in it is licensed by JLCPCB; the file formats (Gerber, IPC-D-356,
positions CSV) are open. It will work at other fabs, though the BOM's LCSC part
numbers are specific to JLCPCB's supplier and would need mapping.

## Bytebeat formulas

The formulas in `firmware/src/formulas.h` were discovered by machine search
(a purpose-built GPU search with a neural fitness model) and are original to this
project, not taken from published bytebeat collections.

One exception: the fallback for an out-of-range slot is the classic
`t*(((t>>12)|(t>>8))&(63&(t>>4)))`, attributed to **Viznut** (Ville-Matias
Heikkilä), who originated bytebeat in 2011. It is included as a well-known
reference tone. Since 1.11 all hundred numbered slots are filled, so it is no
longer reachable from the panel. See
<http://viznut.fi/texts-en/bytebeat_algorithmic_music.html>.
