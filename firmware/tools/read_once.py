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
    # Indices follow struct Telemetry in ogham_main.cpp:
    #   0 magic  1 counter  2 segs  3 modeState  4 ioState  5 out1  6 out2
    #   7 paramA 8 paramB  9 rate  10 extClockRate
    #   11 potA  12 potB   13 potRate  14 potLevel
    #   15 combinedA  16 combinedB  17 cvA  18 cvB  19 cpuPeak ...
    # These were one place out, so every value printed was the next field along
    # and "cvB" was actually cpuPeak -- which is why it read as a five-digit
    # number rather than a 0-1 ADC value. Calibrating against this would have
    # used the wrong channel.
    potA, potB, potR, potL = v[11], v[12], v[13], v[14]
    cA, cB, cvA, cvB = v[15], v[16], v[17], v[18]
    print(f"  potA={potA:.4f}  cvA(adc4)={cvA:.4f}  combinedA={cA:.4f}")
    print(f"  potB={potB:.4f}  cvB(adc5)={cvB:.4f}  combinedB={cB:.4f}")
    print(f"  potRate={potR:.4f}  potLevel={potL:.4f}")
finally:
    r.stop()
