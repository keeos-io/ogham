# Changelog

Firmware releases. Each version is tagged here, with both encoder builds attached
to the corresponding GitHub Release and their SHA-256 published.

Full release notes, and the in-browser flasher, live at
<https://keeos.io/firmware/ogham/>.

## 1.11 — 2026-08-10

The full bank: all 100 slots are now real formulas, grouped by character.

- **The bank is complete and reorganised.** `F0`–`F99` are a hundred curated
  formulas — twenty each of **textural** (`F0`–`F19`), **noise** (`F20`–`F39`),
  **percussive** (`F40`–`F59`), **rhythmic** (`F60`–`F79`) and **melodic**
  (`F80`–`F99`). The Viznut placeholder no longer plays on any numbered slot.
- **Every slot number now means something different.** The previous 22-formula
  bank is entirely replaced, so a saved patch (or a written-down slot number)
  selects a different sound than it did on 1.10. Settings themselves are not
  touched — the layout is unchanged, so updating does **not** reset them — but
  the two saved formula indices will need re-choosing by ear.
- The formulas are generated from the curated library rather than hand-written,
  and each one is verified against the evaluator they were auditioned on before
  it ships: 72 million samples per build, across nine A/B settings and four
  windows of `t`, all matching sample for sample.
- Manual: Appendix A rewritten as the hundred-formula bank, grouped by type,
  listing each slot's number, name and expression.

## 1.10 — 2026-08-08

Maintenance release: no new user-facing features, no settings-layout change
(updating from 1.09 does **not** reset your saved settings).

- Firmware source clean-up — removed several accessors and one code path left
  behind by earlier, since-superseded designs (an unused Out 2 "delay"
  mechanism, an unused engine freeze, unused polled gate/clock edge
  detection, among others). No behaviour change.
- Manual: new Troubleshooting section — the internal LPG being left on with
  nothing patched to Sync produces total silence on both outputs by design,
  not a fault; that's now written down instead of only discoverable by
  re-deriving it live.

## 1.09 — 2026-07-30

First release published from this repository.

- **Internal LPG.** A low-pass gate on the output, off by default, plucked by the
  **Sync** input. One envelope drives a VCA *and* a low-pass filter together, so
  quiet is also dark — the module can play discrete percussive notes from a
  trigger instead of only running continuously. Decay 2 ms – 20 s.
- **Menu navigation.** The field list no longer wraps, it remembers where you
  were, and it accelerates with turn speed.
- **Encoder scanning moved to a dedicated 10 kHz timer.** 1.03 fixed steps being
  lost during display writes; this fixes the remainder, where a fast crank
  outran the 1 kHz scan.

> **Updating from 1.03 or earlier resets the module to factory defaults** — the
> stored-settings layout changed. Both voices return to `F0` and the menu to its
> defaults, once, on the first boot after flashing.

## 1.03 — 2026-07-28

- Firmware version shown for ~1 s at boot, and recorded in stored settings.
- Encoder decoding moved into the audio interrupt, so detents are no longer
  dropped while the display is being written.
- Pot ranges recalibrated across the built units; the Tone control's clockwise
  sweep spread out so the crush and resonance region is easier to land on.
- Two encoder builds introduced, for the two A/B phasings in circulation.

## 1.00

Initial release. Predates the boot version display — if the startup animation
runs straight into the function display with no version, that is what the module
is running.
