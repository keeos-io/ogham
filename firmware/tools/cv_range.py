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

"""Capture the A/B combined + param range while you sweep a CV source.
Polls for DURATION seconds; prints periodic snapshots and a min/max summary."""
import struct, sys, time
import ogham_monitor as m

DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 25.0

r = m.SwdReader()
if not r.start():
    print("CONNECT FAILED (board unpowered / GUI holding ST-Link?)"); sys.exit(1)
addr = m.find_telemetry_addr()

def read():
    w = r.read_words(addr, m.STRUCT_WORDS)
    if len(w) != m.STRUCT_WORDS:
        return None
    v = struct.unpack(m.STRUCT_FMT, b"".join(struct.pack("<I", x) for x in w))
    return v[16], v[17], v[8], v[9]   # combA, combB, paramA, paramB

mnA = mnB = 2.0; mxA = mxB = -1.0
pmnA = pmnB = 9999; pmxA = pmxB = -1
print(f"Sweep your CV now ({DURATION:.0f}s window)...")
t0 = time.time(); last = 0
try:
    while time.time() - t0 < DURATION:
        x = read()
        if x:
            cA, cB, pA, pB = x
            mnA = min(mnA, cA); mxA = max(mxA, cA)
            mnB = min(mnB, cB); mxB = max(mxB, cB)
            pmnA = min(pmnA, pA); pmxA = max(pmxA, pA)
            pmnB = min(pmnB, pB); pmxB = max(pmxB, pB)
            if time.time() - last > 1.5:
                last = time.time()
                print(f"  now  A={cA:.3f}({pA:3d})  B={cB:.3f}({pB:3d})   "
                      f"seen A[{mnA:.3f}..{mxA:.3f}] B[{mnB:.3f}..{mxB:.3f}]")
        time.sleep(0.04)
finally:
    r.stop()
print("--- SWEEP RESULT ---")
print(f"  A: combined {mnA:.3f}..{mxA:.3f}   param {pmnA}..{pmxA}")
print(f"  B: combined {mnB:.3f}..{mxB:.3f}   param {pmnB}..{pmxB}")
