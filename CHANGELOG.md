# Changelog

Firmware releases. Each version is tagged here, with both encoder builds attached
to the corresponding GitHub Release and their SHA-256 published.

Full release notes, and the in-browser flasher, live at
<https://keeos.co.uk/firmware/ogham/>.

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
