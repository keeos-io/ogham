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

"""One-shot telemetry read (for calibration). Prints the A/B channel + pots."""
import struct, sys
import ogham_monitor as m

r = m.SwdReader()
if not r.start():
    print("CONNECT FAILED (board unpowered, or the monitor GUI is holding the ST-Link?)")
    sys.exit(1)
try:
    addr = m.find_telemetry_addr()
    w = r.read_words(addr, m.STRUCT_WORDS)
    if len(w) != m.STRUCT_WORDS:
        print("read failed:", len(w), "words"); sys.exit(1)
    v = struct.unpack(m.STRUCT_FMT, b"".join(struct.pack("<I", x) for x in w))
    potA, potB, potR, potL = v[12], v[13], v[14], v[15]
    cA, cB, cvA, cvB = v[16], v[17], v[18], v[19]
    print(f"  potA={potA:.4f}  cvA(adc4)={cvA:.4f}  combinedA={cA:.4f}")
    print(f"  potB={potB:.4f}  cvB(adc5)={cvB:.4f}  combinedB={cB:.4f}")
    print(f"  potRate={potR:.4f}  potLevel={potL:.4f}")
finally:
    r.stop()
