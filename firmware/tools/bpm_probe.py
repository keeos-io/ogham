#!/usr/bin/env python
# -----------------------------------------------------------------------------
# Ogham — a dual-voice bytebeat synthesizer for Eurorack
#
# Author:     Steven Collins, 2026, Keeos.io
# Copyright:  (c) 2026 Steven Collins
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
#
# This file is part of the Ogham firmware. See LICENSE-firmware.txt at the
# repository root for the full licence text.
# https://github.com/stevec64/keeos-ogham
# -----------------------------------------------------------------------------

"""Watch the BPM estimator live over SWD (daisy-3q1).

Prints detected tempo, the rate-normalised constant, confidence and lock state,
alongside the playback rate and formula so the relationships can be checked:

  bpm should equal baseBpm * rate at all times
  baseBpm should be a property of the formula and hold still as rate changes
  confidence should be > 0 once locked by agreement, 0 on a fallback lock

Usage:  python bpm_probe.py [seconds] [interval]
"""
import struct, sys, time
import ogham_monitor as m

# Telemetry with the BPM fields appended after fxType.
FMT   = "<5I4i10f3Ii" + "3fI"
WORDS = 27

seconds  = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5

r = m.SwdReader()
if not r.start():
    print("CONNECT FAILED (board unpowered, or the monitor GUI is holding the ST-Link?)")
    sys.exit(1)

try:
    addr = m.find_telemetry_addr()
    print(f"g_telemetry @ 0x{addr:08X}")
    print(f"{'t':>6} {'mode':>5} {'formula':>8} {'A':>5} {'B':>5} {'rate':>8} {'extClk':>8} "
          f"{'bpm':>8} {'baseBpm':>8} {'conf':>5} {'lock':>4}")

    t0 = time.time()
    while time.time() - t0 < seconds:
        w = r.read_words(addr, WORDS)
        if len(w) != WORDS:
            print("read failed:", len(w), "words")
            break
        v = struct.unpack(FMT, b"".join(struct.pack("<I", x) for x in w))
        if v[0] != 0x4F474841:
            # Not written yet: the target is still booting after a reset. Wait
            # rather than give up, so acquisition can be watched from cold.
            time.sleep(interval)
            continue

        mode = "VOCT" if (v[3] >> 17) & 1 else "run"
        f1   = v[5]
        pA, pB = v[7], v[8]
        rate = v[9]
        ext  = v[10]
        bpm, base, conf, lock = v[23], v[24], v[25], v[26]

        # bpm must track baseBpm * rate exactly; anything else means the
        # normalisation is not doing what it claims.
        resid = bpm - base * rate

        print(f"{time.time()-t0:6.1f} {mode:>5} {f1:8d} {pA:5d} {pB:5d} {rate:8.4f} {ext:8.4f} "
              f"{bpm:8.2f} {base:8.2f} {conf:5.2f} {lock:4d}")
        time.sleep(interval)
finally:
    r.stop()
