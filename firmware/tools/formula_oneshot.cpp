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
// https://github.com/keeos-io/ogham
// -----------------------------------------------------------------------------
//
// Which formulas are effectively one-shots?
//
// Criterion: after the formula's initial activity, is there ever more than four
// seconds of continuous silence? Residual noises later do not matter -- if you
// have to wait more than four seconds for one, the formula behaves as a one-shot
// on the panel.
//
// "Silence" is measured on the AC part of the output, because the outputs are
// AC-coupled: a formula pinned at a constant value is silent at the jack however
// extreme that value is.
//
// Timing uses each formula's own baseSampleRate, so a gap in ticks converts to
// real seconds correctly for the 11025/16000/32000/44100 entries as well as the
// 8000 ones.
//
// Build and run from hardware/ogham/firmware:
//   cl /O2 /EHsc /I src /Fe:build_host\formula_oneshot.exe /Fo:build_host\ ^
//      tools\formula_oneshot.cpp src\formulas.cpp
//   build_host\formula_oneshot.exe

#include "formulas.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

const FormulaInfo* GetFormulaAt(int index);
int GetFormulaCount();

static const int   kWin = 2048;        // 256 ms at 8 kHz -- resolves a gap edge
static const double kQuietRms = 1.0;   // LSB of 255; about -48 dBFS
static const double kGapSecs = 4.0;    // the stated threshold
static const double kHorizonSecs = 200.0;  // past this, it is not a one-shot

// How much of the A/B plane has to behave as a one-shot before the formula
// counts as one. Set at the natural gap in the measured data rather than a round
// number: the ranking runs 100, 84, 80, 80, 68, 64... down to 48 and 36, and
// then falls straight to 20 with nothing in between. Anything at or above a
// third of the plane is a formula you will meet as a one-shot in normal use;
// below that it is a corner case.
static const double kOneShotPct = 30.0;

// Result for one (A,B) cell.
struct Cell {
    bool everSounded;
    bool oneShot;      // a gap longer than kGapSecs, after some activity
    double endSecs;    // when the last activity before that gap stopped
};

static Cell ScanCell(const FormulaInfo* f, int32_t a, int32_t b,
                     std::vector<uint8_t>& buf) {
    const double sr = (double)f->baseSampleRate;
    const int maxWin = (int)((kHorizonSecs * sr) / kWin);
    const int gapWins = (int)std::ceil((kGapSecs * sr) / kWin);

    Cell c = {false, false, 0.0};
    int quietRun = 0;
    double lastLoudEnd = 0.0;

    for (int w = 0; w < maxWin; w++) {
        const uint32_t t0 = (uint32_t)w * (uint32_t)kWin;
        double sum = 0.0;
        for (int i = 0; i < kWin; i++) {
            buf[i] = f->func(t0 + (uint32_t)i, a, b);
            sum += buf[i];
        }
        const double mean = sum / kWin;
        double acc = 0.0;
        for (int i = 0; i < kWin; i++) {
            const double d = (double)buf[i] - mean;
            acc += d * d;
        }
        const bool loud = std::sqrt(acc / kWin) >= kQuietRms;

        if (loud) {
            c.everSounded = true;
            quietRun = 0;
            lastLoudEnd = (double)((uint32_t)(w + 1) * (uint32_t)kWin) / sr;
        } else if (c.everSounded) {
            if (++quietRun >= gapWins) {
                c.oneShot = true;
                c.endSecs = lastLoudEnd;
                return c;             // gap found: no need to scan further
            }
        }
    }
    return c;
}

int main() {
    // A/B grid. Five values each way is enough to see whether the behaviour is
    // a property of the formula or of a particular corner of the plane.
    const int kAB[] = {0, 64, 128, 192, 255};
    const int kABn = sizeof(kAB) / sizeof(kAB[0]);

    printf("%-4s %-24s %7s %9s  %s\n",
           "idx", "name", "%oneshot", "hit ends", "verdict");

    std::vector<uint8_t> buf(kWin);
    std::vector<int> oneShot, partial, dead;

    for (int idx = 0; idx < GetFormulaCount(); idx++) {
        const FormulaInfo* f = GetFormulaAt(idx);
        if (!f || !f->func) continue;

        int cells = 0, os = 0, silent = 0;
        std::vector<double> ends;

        for (int ia = 0; ia < kABn; ia++) {
            for (int ib = 0; ib < kABn; ib++) {
                Cell c = ScanCell(f, kAB[ia], kAB[ib], buf);
                cells++;
                if (!c.everSounded) silent++;
                if (c.oneShot) { os++; ends.push_back(c.endSecs); }
            }
        }

        const double pct = 100.0 * os / cells;
        double medEnd = 0.0;
        if (!ends.empty()) {
            std::sort(ends.begin(), ends.end());
            medEnd = ends[ends.size() / 2];
        }

        const char* verdict;
        if (silent == cells)          { verdict = "NEVER SOUNDS";      dead.push_back(idx); }
        else if (pct >= kOneShotPct)  { verdict = "ONE-SHOT";          oneShot.push_back(idx); }
        else if (pct > 0.0)       { verdict = "one-shot at some A/B"; partial.push_back(idx); }
        else                      { verdict = "sustains"; }

        if (medEnd > 0.0)
            printf("%-4d %-24.24s %6.0f%% %8.2fs  %s\n", idx, f->name, pct, medEnd, verdict);
        else
            printf("%-4d %-24.24s %6.0f%% %9s  %s\n", idx, f->name, pct, "-", verdict);
    }

    printf("\n=== EFFECTIVELY ONE-SHOT (>=%.0f%% of the A/B plane) ===\n", kOneShotPct);
    for (size_t i = 0; i < oneShot.size(); i++)
        printf("%s%d", i ? ", " : "", oneShot[i]);
    printf("\n  (%d formulas)\n", (int)oneShot.size());

    printf("\n=== ONE-SHOT ONLY AT SOME A/B ===\n");
    for (size_t i = 0; i < partial.size(); i++)
        printf("%s%d", i ? ", " : "", partial[i]);
    printf("\n  (%d formulas)\n", (int)partial.size());

    if (!dead.empty()) {
        printf("\n=== NEVER SOUND AT ANY A/B TESTED ===\n");
        for (size_t i = 0; i < dead.size(); i++)
            printf("%s%d", i ? ", " : "", dead[i]);
        printf("\n  (%d formulas)\n", (int)dead.size());
    }
    return 0;
}
