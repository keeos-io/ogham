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
// Host-side survey: does each formula have a fundamental worth tracking?
//
// Motivation: a proposed V/oct mode that drives the *playback rate* rather than
// hard-syncing. A periodic component of period P ticks, played at R ticks/sec,
// sounds at R/P Hz -- so an exponential R gives exact V/oct tracking of that
// component, with every partial scaling together (varispeed transposition).
// Formulas with a stable fundamental become melodic; the rest degrade to an
// exponential speed control, which is still useful.
//
// This measures, per formula: the dominant period found by normalised
// autocorrelation of its free-running output, the resulting natural pitch at
// rate 1.0, how strong that periodicity is, and how stable it is across time
// and across A/B. Stability matters most -- a formula whose period drifts as t
// grows will not hold a note.
//
// Build and run from hardware/ogham/firmware (see tools/README.md):
//   cl /O2 /EHsc /I src /Fe:build_host\voct_tone.exe tools\voct_tone.cpp src\formulas.cpp
//   build_host\voct_tone.exe

#include "formulas.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

const FormulaInfo* GetFormulaAt(int index);
int GetFormulaCount();

static const double SR_CORRECTION = 28160.0 / 28154.0;
static const double VOCT_BASE_HZ  = 32.70 / SR_CORRECTION;   // C1

static const char* kNoteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

static void NoteFor(double hz, char* out, size_t n) {
    if (hz <= 0.0) { snprintf(out, n, "-"); return; }
    double semis = 12.0 * std::log2(hz / VOCT_BASE_HZ);
    int    s     = (int)std::floor(semis + 0.5);
    int    oct   = 1 + (s >= 0 ? s / 12 : (s - 11) / 12);
    int    pc    = ((s % 12) + 12) % 12;
    snprintf(out, n, "%s%d", kNoteNames[pc], oct);
}

// Dominant period of buf[] by normalised autocorrelation over [minLag,maxLag].
// Returns the lag; *corr gets the normalised peak in [-1,1].
static int DominantLag(const float* buf, int n, int minLag, int maxLag, float* corr) {
    double mean = 0.0;
    for (int i = 0; i < n; i++) mean += buf[i];
    mean /= n;

    std::vector<float> d(n);
    double energy = 0.0;
    for (int i = 0; i < n; i++) { d[i] = (float)(buf[i] - mean); energy += d[i] * d[i]; }
    *corr = 0.0f;
    if (energy < 1e-9) return 0;

    int   bestLag = 0;
    double best   = -2.0;
    for (int lag = minLag; lag <= maxLag && lag < n / 2; lag++) {
        const int count = n - lag;
        double sum = 0.0, e2 = 0.0;
        for (int i = 0; i < count; i++) { sum += (double)d[i] * d[i + lag]; e2 += (double)d[i + lag] * d[i + lag]; }
        double denom = std::sqrt(energy * e2 + 1e-12);
        double c = (denom > 0.0) ? sum / denom : 0.0;
        if (c > best) { best = c; bestLag = lag; }
    }
    *corr = (float)best;
    return bestLag;
}

int main() {
    const int kAB[]  = {64, 128, 192};
    const int kABn   = 3;
    const int kWin   = 8192;        // ticks analysed per chunk
    const int kChunks = 4;          // consecutive chunks, to test stability
    const int kSkip  = 4096;        // skip the intro; the proposal lets it play out
    const int kMinLag = 2, kMaxLag = 1024;

    printf("%-4s %-24s %8s %8s  %-5s %6s %6s  %s\n",
           "idx", "name", "natHz", "period", "note", "corr", "stab", "verdict");

    int melodic = 0, weak = 0, none = 0, total = 0;
    std::vector<float> buf(kWin);

    for (int idx = 0; idx < GetFormulaCount(); idx++) {
        const FormulaInfo* f = GetFormulaAt(idx);
        if (!f || !f->func) continue;
        total++;

        // Collect a period estimate per (A/B, chunk).
        std::vector<double> hz;
        double corrSum = 0.0; int corrN = 0;

        for (int ia = 0; ia < kABn; ia++) {
            for (int ib = 0; ib < kABn; ib++) {
                for (int c = 0; c < kChunks; c++) {
                    const uint32_t t0 = (uint32_t)(kSkip + c * kWin);
                    for (int i = 0; i < kWin; i++)
                        buf[i] = (float)f->func(t0 + (uint32_t)i, kAB[ia], kAB[ib]);
                    float corr = 0.0f;
                    int lag = DominantLag(buf.data(), kWin, kMinLag, kMaxLag, &corr);
                    corrSum += corr; corrN++;
                    if (lag > 0 && corr > 0.3f) hz.push_back((double)f->baseSampleRate / lag);
                }
            }
        }

        const double meanCorr = corrN ? corrSum / corrN : 0.0;

        // Stability: fraction of estimates within a semitone of the median.
        double stab = 0.0, medHz = 0.0;
        if (!hz.empty()) {
            std::vector<double> s = hz;
            std::sort(s.begin(), s.end());
            medHz = s[s.size() / 2];
            int agree = 0;
            for (double v : hz) if (std::fabs(std::log2(v / medHz)) < 1.0 / 12.0) agree++;
            // Denominator is every attempt, so estimates that failed the corr
            // gate count against stability rather than being quietly dropped.
            stab = (double)agree / (double)corrN;
        }

        char note[8] = "-";
        if (medHz > 0.0) NoteFor(medHz, note, sizeof(note));

        const char* verdict;
        if (medHz > 0.0 && meanCorr > 0.5 && stab > 0.7)      { verdict = "MELODIC";        melodic++; }
        else if (medHz > 0.0 && meanCorr > 0.3 && stab > 0.4) { verdict = "weak/unstable";  weak++;    }
        else                                                  { verdict = "no fundamental"; none++;    }

        printf("%-4d %-24.24s %8.1f %8.1f  %-5s %6.2f %6.2f  %s\n",
               idx, f->name, medHz,
               medHz > 0.0 ? (double)f->baseSampleRate / medHz : 0.0,
               note, meanCorr, stab, verdict);
    }

    printf("\n%d melodic, %d weak/unstable, %d with no fundamental (of %d)\n",
           melodic, weak, none, total);
    printf("Pitch shown is at rate 1.0; V/oct would scale it, so the rate knob\n"
           "sets which octave a given CV lands in.\n");
    return 0;
}
