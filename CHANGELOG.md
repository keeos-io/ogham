# Changelog

Firmware releases. Each version is tagged here, with both encoder builds attached
to the corresponding GitHub Release and their SHA-256 published.

Full release notes, and the in-browser flasher, live at
<https://keeos.io/firmware/ogham/>.

## 1.16 — 2026-08-18

Housekeeping ahead of the sources being published: the unreachable placeholder
formula is gone.

- **The out-of-range fallback is removed.** Every numbered slot has held a real
  formula since 1.11, and both voices clamp their index before the lookup, so
  the placeholder could not be reached from the panel or by any tool. An
  out-of-range index now returns slot 0. Nothing sounds different; 48 bytes of
  flash recovered, leaving the build at 127,712 B (97.44%).
- Settings are untouched — the stored layout is unchanged.

## 1.15 — 2026-08-18

The A and B knobs get their top fifth back, one-shot formulas fire when you
select them, and a menu field added the day before is withdrawn.

- **A and B stopped climbing at 2 o'clock.** Both knobs reached 255 at about 80%
  of travel, on every module, and the last fifth of the rotation did nothing.
  The pot and its CV were summed in hardware into a single ADC, and that sum sits
  on the 0 V rail before the pot finishes its sweep — by the time the firmware
  saw it, the information was gone. The pot is now read from its own ADC and the
  CV recovered as the sum's departure from the fitted line: 255 arrives at 100%
  of travel on both channels. Bipolar CV behaviour is unchanged, measured
  symmetric at +83/−82 counts about the pot's position. Channel A had also
  carried a ~1% scale error against B since the original port.
- **Selecting a one-shot fires it.** Roughly a fifth of the bank is percussive
  one-shots that make their sound within the first few thousand ticks and are
  silent ever after, so landing on one after a few minutes of playing gave
  nothing at all — at every A and B, with no indication why. Changing either
  voice's formula now restarts the waveform. A decoupled Out 2 drone is not
  affected.
- **The V/oct start offset (`P`) field is withdrawn**, added and removed the same
  day. Skipping a formula's silent opening is better described in the manual than
  dialled in from a menu, so the Sync chapter gains a section on formulas that
  start silent — the first remedy being to turn Rate up. Settings are *not*
  reset: the persisted byte was kept as a reserved field, so the stored layout
  and `SETTINGS_VERSION` 16 are unchanged.
- **Appendix A marks the one-shots.** Every formula in the manual now says
  whether it plays continuously, one-shots, or one-shots only at some A/B
  settings, classified by measurement across the whole bank rather than by ear.
- Flash: 127,760 B, 97.47% of the 128 KB region.

## 1.14 — 2026-08-11

Pitch, tempo and the low-pass gate all change here.

> **This update resets your settings.** The stored layout changed, so every
> module returns to factory defaults on first boot: both voices to `F0`, and the
> whole menu — FX chain, LPG, ENV Out — back to defaults.

- **V/oct transposes instead of hard-syncing.** It previously reset the formula
  every cycle to force a pitch, which gave a clean note but discarded most of
  what the formula was doing, and silenced plenty of formulas outright. It now
  scales the playback rate exponentially, so every partial moves together —
  varispeed transposition rather than a per-cycle reset. The Rate knob becomes a
  quantized octave placement, ÷32 to ×32, which is what puts a formula into a
  register you can play.
- **Clock In assumes one pulse per beat**, not per sixteenth — what most gear
  actually sends. At the same knob position a patched clock now plays about four
  times slower and sits much closer to native speed. The dropout timeouts moved
  from 2 s to 10 s with it: a 4/4 bar at 120 BPM is exactly 2 s, so a slow clock
  had been reading as a stopped one.
- **The LPG decay knee moves to 40 = 500 ms** (it was 50 = 10 s). The bottom
  third of the dial had crammed a 166× range into 30 numbers while the top half
  spent 49 values on a 2× range nobody could hear. A stored patch keeps its
  number but sounds different.
- **A and B recover their odd numbers** — a resolution loss in the parameter
  path, separate from the range problem fixed in 1.15.

## 1.13 — 2026-08-10

The encoder stops missing clicks, and effects you are not using stop costing you
CPU. Settings are not reset.

- **The encoder no longer drops small movements.** Its scan ran on the
  lowest-priority timer interrupt in the system, below the audio callback: at
  light load that cost nothing, but under a full FX chain the effective scan rate
  collapsed by nearly half, and a brisk turn crossed two positions between looks
  — at which point the movement was discarded rather than delayed. The scan
  interrupt now outranks the audio DMA. Measured on the same gesture: 21 detents
  decoded under load, against 6 before.
- **A stage at Level 0 is skipped, not just mixed out.** All three FX stages were
  evaluated and then discarded, so a one-effect patch paid for the whole chain. A
  typical one-effect patch now runs at about 20% CPU. Stage phasors keep running
  while skipped, so a sweep does not jump when its level comes back up.
- v1.12 was a bench build flashed to one module during the encoder
  investigation, and is folded into this release.

## 1.11 — 2026-08-10

The full bank: all 100 slots are now real formulas, grouped by character.

- **The bank is complete and reorganised.** `F0`–`F99` are a hundred curated
  formulas — twenty each of **textural** (`F0`–`F19`), **noise** (`F20`–`F39`),
  **percussive** (`F40`–`F59`), **rhythmic** (`F60`–`F79`) and **melodic**
  (`F80`–`F99`). The placeholder no longer plays on any numbered slot.
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
