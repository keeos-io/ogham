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
// Host-side survey: how loud is each formula, and does it stay loud?
//
// The engine maps a formula's byte to audio as (v / 127.5) - 1 and the outputs
// are AC-coupled, so only the *varying* part is audible: a formula pinned at a
// constant value is silence at the jack however extreme that value is.
//
// Crucially this measures two windows. `t` is never reset by changing formula --
// only the Sync input resets it -- so `t` is simply however many ticks have
// elapsed since power-on: about 4.8 million after ten minutes at 8 kHz. A
// one-shot formula sounds at small `t` and is long finished by then. Comparing
// an early window against a late one shows which formulas are one-shots.
//
// Build and run from hardware/ogham/firmware:
//   cl /O2 /EHsc /I src /Fe:build_host\formula_level.exe /Fo:build_host\ ^
//      tools\formula_level.cpp src\formulas.cpp
//   build_host\formula_level.exe

#include "formulas.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

const FormulaInfo* GetFormulaAt(int index);
int GetFormulaCount();

// Reported by the user as producing little or no audio at most A/B.
static const int kReported[] = {16, 27, 37, 39, 40, 41, 42, 48,
                                50, 51, 52, 53, 54, 55, 56, 58, 59, 96};
static bool IsReported(int idx) {
    for (int v : kReported) if (v == idx) return true;
    return false;
}

// AC RMS of a formula over kN ticks from t0, in LSB of the 0-255 byte.
static double AcRms(const FormulaInfo* f, uint32_t t0, int n,
                    int32_t a, int32_t b, std::vector<uint8_t>& buf) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        buf[i] = f->func(t0 + (uint32_t)i, a, b);
        sum += buf[i];
    }
    const double mean = sum / n;
    double acc = 0.0;
    for (int i = 0; i < n; i++) {
        const double d = (double)buf[i] - mean;
        acc += d * d;
    }
    return std::sqrt(acc / n);
}

int main() {
    const int kAB[] = {0, 32, 64, 96, 128, 160, 192, 224, 255};
    const int kABn = sizeof(kAB) / sizeof(kAB[0]);
    const int kN = 16384;               // ~2 s at 8 kHz

    // Early: just after a sync reset. Late: where t sits after ten minutes of
    // running, which is what a user actually hears when they select a formula.
    const uint32_t kEarly = 512;
    const uint32_t kLate = 4800000;

    // 1 LSB RMS is about -48 dBFS against a 255 peak-to-peak full scale: audible
    // but very quiet. Below that, treat the cell as dead.
    const double kQuiet = 1.0;

    printf("%-4s %-24s %8s %8s %8s  %s\n",
           "idx", "name", "early", "late", "late/early", "verdict");

    std::vector<uint8_t> buf(kN);
    int repOneShot = 0, othOneShot = 0, repTotal = 0, othTotal = 0;
    std::vector<int> oneShots;

    for (int idx = 0; idx < GetFormulaCount(); idx++) {
        const FormulaInfo* f = GetFormulaAt(idx);
        if (!f || !f->func) continue;

        std::vector<double> e, l;
        int deadLate = 0, cells = 0;
        for (int ia = 0; ia < kABn; ia++) {
            for (int ib = 0; ib < kABn; ib++) {
                const int32_t a = kAB[ia], b = kAB[ib];
                e.push_back(AcRms(f, kEarly, kN, a, b, buf));
                const double rl = AcRms(f, kLate, kN, a, b, buf);
                l.push_back(rl);
                cells++;
                if (rl < kQuiet) deadLate++;
            }
        }
        std::sort(e.begin(), e.end());
        std::sort(l.begin(), l.end());
        const double me = e[e.size() / 2], ml = l[l.size() / 2];
        const double ratio = (me > 0.01) ? ml / me : 1.0;
        const double pctDead = 100.0 * deadLate / cells;

        // A one-shot: lively near t = 0, dead once t has run on.
        const bool oneShot = (me >= kQuiet) && (pctDead > 50.0);
        const char* verdict = oneShot ? "ONE-SHOT (dead at large t)"
                            : (pctDead > 20.0 ? "patchy at large t" : "sustains");

        if (IsReported(idx)) { repTotal++; if (oneShot) repOneShot++; }
        else                 { othTotal++; if (oneShot) othOneShot++; }
        if (oneShot) oneShots.push_back(idx);

        printf("%-4d %-24.24s %8.2f %8.2f %9.3f  %s%s\n",
               idx, f->name, me, ml, ratio, verdict,
               IsReported(idx) ? "   <- reported" : "");
    }

    printf("\nOne-shots (lively at small t, dead at large t): ");
    for (size_t i = 0; i < oneShots.size(); i++)
        printf("%s%d", i ? ", " : "", oneShots[i]);
    printf("\n\n");
    printf("Of the %d reported, %d are one-shots.\n", repTotal, repOneShot);
    printf("Of the %d others,   %d are one-shots.\n", othTotal, othOneShot);
    return 0;
}
