// -----------------------------------------------------------------------------
// Ogham — a dual-voice bytebeat synthesizer for Eurorack
//
// Author:     Steven Collins, 2026, Keeos.io
// Copyright:  (c) 2026 Steven Collins
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
//
// This file is part of the Ogham firmware. See LICENSE-firmware.txt at the
// repository root for the full licence text.
// https://github.com/stevec64/keeos-ogham
// -----------------------------------------------------------------------------
//
// Host-side survey: the highest V/oct pitch at which each formula still makes
// sound.
//
// In V/oct mode the engine hard-syncs -- `t` is reset to 0 every 1/f seconds --
// so the formula is only ever evaluated over t = 0 .. baseSampleRate/f. A
// formula whose output is constant over that opening window is silent, however
// lively it is when free-running. This computes, for each formula, the smallest
// t at which the output first differs from its t=0 value, and converts that to
// the pitch above which the formula goes quiet.
//
// Build and run from hardware/ogham/firmware:
//   g++ -O2 -I src -o /tmp/voct_range tools/voct_range.cpp src/formulas.cpp
//   /tmp/voct_range

#include "formulas.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>

const FormulaInfo* GetFormulaAt(int index);
int GetFormulaCount();

// Matches ogham_main.cpp
static const double SR_CORRECTION = 28160.0 / 28154.0;
static const double VOCT_BASE_HZ  = 32.70 / SR_CORRECTION;   // 0 V = C1

static const char* kNoteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// Nearest note name for a frequency, relative to C1 at VOCT_BASE_HZ.
static void NoteFor(double hz, char* out, size_t n) {
    if (hz <= 0.0) { snprintf(out, n, "-"); return; }
    double semis = 12.0 * std::log2(hz / VOCT_BASE_HZ);
    int    s     = (int)std::floor(semis + 0.5);
    int    oct   = 1 + (s >= 0 ? s / 12 : (s - 11) / 12);
    int    pc    = ((s % 12) + 12) % 12;
    snprintf(out, n, "%s%d", kNoteNames[pc], oct);
}

int main() {
    // Sweep A/B: the first-change point can depend on them, so report the worst
    // case (the largest t needed) across a grid.
    const int kAB[] = {0, 32, 64, 96, 128, 160, 192, 224, 255};
    const int kABn  = sizeof(kAB) / sizeof(kAB[0]);
    const uint32_t kMaxT = 200000;   // far past any sane sync window

    printf("%-4s %-24s %6s %8s %8s  %-6s  %s\n",
           "idx", "name", "baseSR", "min win", "max Hz", "note", "verdict");

    int silentAtC3 = 0, silentAlways = 0, total = 0;
    const double c3 = VOCT_BASE_HZ * 4.0;   // C1 + 2 octaves
    const int kAudibleP2P = 2;              // p-p in 0-255 below this is inaudible

    for (int idx = 0; idx < GetFormulaCount(); idx++) {
        const FormulaInfo* f = GetFormulaAt(idx);
        if (!f || !f->func) continue;
        total++;

        // Smallest window over which the output swings *audibly*. Peak-to-peak
        // over t = 0..W-1 only grows with W, so a single scan finds it. Worst
        // case across the A/B grid, since a formula that needs a wider window
        // at some A/B setting will fall silent there.
        uint32_t worst = 0;
        bool     everVaries = false;

        for (int ia = 0; ia < kABn; ia++) {
            for (int ib = 0; ib < kABn; ib++) {
                const int32_t a = kAB[ia], b = kAB[ib];
                int lo = 255, hi = 0;
                uint32_t needW = 0;
                for (uint32_t t = 0; t < kMaxT; t++) {
                    const int v = f->func(t, a, b);
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                    if (hi - lo > kAudibleP2P) { needW = t + 1; break; }
                }
                if (needW == 0) continue;       // never swings audibly here
                everVaries = true;
                if (needW > worst) worst = needW;
            }
        }

        char note[8] = "-";
        double maxHz = 0.0;
        const char* verdict = "silent at every pitch";
        if (everVaries && worst > 0) {
            // t reaches baseSampleRate / f, so f <= baseSampleRate / worst.
            maxHz = (double)f->baseSampleRate / (double)worst;
            NoteFor(maxHz, note, sizeof(note));
            verdict = (maxHz >= c3)            ? "ok at C3"
                    : (maxHz >= VOCT_BASE_HZ)  ? "silent at C3, ok lower"
                                               : "SILENT at every pitch";
            if (maxHz < c3)           silentAtC3++;
            if (maxHz < VOCT_BASE_HZ) silentAlways++;
        } else {
            silentAtC3++;
            silentAlways++;
        }

        printf("%-4d %-24.24s %6d %8u %8.1f  %-6s  %s\n",
               idx, f->name, f->baseSampleRate, worst, maxHz, note, verdict);
    }

    printf("\nC1 = %.2f Hz (0 V), C3 = %.2f Hz\n", VOCT_BASE_HZ, c3);
    printf("%d of %d formulas are silent at C3 or above.\n", silentAtC3, total);
    printf("%d of %d are silent at EVERY playable V/oct pitch (C1 and up).\n",
           silentAlways, total);
    return 0;
}
