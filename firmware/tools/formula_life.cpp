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
// How long does a one-shot formula sound for after a reset?
//
// `t` only ever restarts on a Sync edge, so a formula that stops varying after
// N ticks is audible for N / baseSampleRate seconds and then silent until the
// next Sync. That number is what tells you how fast to clock Sync to keep the
// formula alive.
//
// Walks consecutive windows from t = 0 and reports the first window whose AC
// level falls below audibility, median across an A/B grid.
//
// Build and run from hardware/ogham/firmware:
//   cl /O2 /EHsc /I src /Fe:build_host\formula_life.exe /Fo:build_host\ ^
//      tools\formula_life.cpp src\formulas.cpp
//   build_host\formula_life.exe

#include "formulas.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

const FormulaInfo* GetFormulaAt(int index);
int GetFormulaCount();

static const int kReported[] = {16, 27, 37, 39, 40, 41, 42, 48,
                                50, 51, 52, 53, 54, 55, 56, 58, 59, 96};

int main() {
    const int kAB[] = {0, 64, 128, 192, 255};
    const int kABn = sizeof(kAB) / sizeof(kAB[0]);
    const int kWin = 512;            // 64 ms at 8 kHz -- fine enough to time a hit
    const int kMaxWin = 4000;        // out to t = 2,048,000 (~4 min)
    const double kQuiet = 1.0;       // LSB RMS, about -48 dBFS

    printf("%-4s %-24s %10s %10s  %s\n",
           "idx", "name", "lives to t", "seconds", "note");

    std::vector<uint8_t> buf(kWin);

    for (int r = 0; r < (int)(sizeof(kReported) / sizeof(kReported[0])); r++) {
        const int idx = kReported[r];
        const FormulaInfo* f = GetFormulaAt(idx);
        if (!f || !f->func) continue;

        std::vector<double> lives;
        for (int ia = 0; ia < kABn; ia++) {
            for (int ib = 0; ib < kABn; ib++) {
                const int32_t a = kAB[ia], b = kAB[ib];
                uint32_t died = (uint32_t)kMaxWin * kWin;   // "never" sentinel
                int quietRun = 0;
                for (int w = 0; w < kMaxWin; w++) {
                    const uint32_t t0 = (uint32_t)w * kWin;
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
                    // Require several consecutive quiet windows, so a momentary
                    // gap inside a rhythm is not mistaken for the end.
                    if (std::sqrt(acc / kWin) < kQuiet) {
                        if (++quietRun >= 4) { died = t0 - (uint32_t)(3 * kWin); break; }
                    } else {
                        quietRun = 0;
                    }
                }
                lives.push_back((double)died);
            }
        }
        std::sort(lives.begin(), lives.end());
        const double med = lives[lives.size() / 2];
        const double secs = med / (double)f->baseSampleRate;
        const bool never = med >= (double)kMaxWin * kWin;

        printf("%-4d %-24.24s %10.0f %10.2f  %s\n",
               idx, f->name, med, secs,
               never ? "keeps going past 4 min" : "one-shot: needs Sync");
    }
    return 0;
}
