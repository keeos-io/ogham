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

#pragma once
#include <cstdint>

// ByteBeat formula function signature
// All formulas take t (time), a (parameter A), b (parameter B)
// and return an 8-bit audio sample
typedef uint8_t (*BytebeatFormula)(uint32_t t, int32_t a, int32_t b);

// Formula metadata
struct FormulaInfo {
    const char* name;
    const char* author;
    BytebeatFormula func;
    int baseSampleRate;     // 8000, 11025, 16000, 32000, or 44100
    int32_t defaultA;       // Default A value (original constant)
    int32_t defaultB;       // Default B value (original constant)
    int32_t minA, maxA;     // Useful range for A
    int32_t minB, maxB;     // Useful range for B
};

// --- JS-compatible shift helpers ---
// Mask shift amount to 0-31 to match JavaScript semantics

inline int32_t shr(int32_t val, int32_t shift) {
    return val >> (shift & 31);
}

inline int32_t shl(int32_t val, int32_t shift) {
    return val << (shift & 31);
}

// Safe division (avoid div-by-zero, match JS truncation)
inline int32_t safediv(int32_t num, int32_t den) {
    if (den == 0) return 0;
    return num / den;
}

// Safe modulo (avoid div-by-zero)
inline int32_t safemod(int32_t num, int32_t den) {
    if (den == 0) return 0;
    return num % den;
}

// --- Compiled formula functions ---
// Evolutionary-optimised bytebeat formulas (160-189) by stevec64
// Each formula is parameterized with A and B for knob control.

// #01: Formula 160 - bass throb
// Original: t | (238 << t | t / 123)
// A=238 (shift base), B=123 (divisor)
static inline uint8_t formula_bass_throb(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(i | (shl(a, i) | safediv(i, b))) & 0xFF;
}

// #02: Formula 168 - malfunction
// Original: t / 15 | ((t / 15 & t) / 15 | t * 112)
// A=15 (divisor), B=112 (multiply)
static inline uint8_t formula_malfunction(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    int32_t d = safediv(i, a);
    return (uint8_t)(d | (safediv(d & i, a) | i * b)) & 0xFF;
}

// #03: Formula 169 - running seq
// Original: t / 123 & (32768 * 141 + 187 ^ 159 * t)
// = (t/123) & (4620475 ^ (159*t))
// A=123 (divisor), B=159 (multiply)
static inline uint8_t formula_running_seq(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(safediv(i, a) & (4620475 ^ (b * i))) & 0xFF;
}

// #04: Formula 170 - basey sequence
// Original: t / 123 & (t | t * 133)
// A=123 (divisor), B=133 (multiply)
static inline uint8_t formula_basey_seq(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(safediv(i, a) & (i | i * b)) & 0xFF;
}

// #05: Formula 171 - morse code
// Original: t / 131 << (t | 13 & 205) / 500 / (129 & t)
// = (t/131) << (((t|13)/500) / (129&t))
// A=131 (base freq), B=500 (timing divisor)
static inline uint8_t formula_morse_code(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)shl(safediv(i, a),
                        safediv(safediv(i | 13, b), (129 & i))) & 0xFF;
}

// #06: Formula 172 - Noisy robot
// Original: t * (t % 3000 * (t >> (t >> 74))) ^ (193 | t)
// A=3000 (modulo pattern length), B=193 (OR mask)
static inline uint8_t formula_noisy_robot(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)((i * (safemod(i, a) * shr(i, shr(i, 74)))) ^ (b | i)) & 0xFF;
}

// #07: Formula 173 - harsh harmony
// Original: ((161 >> (155 & t)) + t - 64 ^ t) & t / 122
// = ((shr(161, 155&t) + t - 64) ^ t) & (t/122)
// A=155 (AND mask), B=122 (divisor)
static inline uint8_t formula_harsh_harm(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(((shr(161, a & i) + i - 64) ^ i) & safediv(i, b)) & 0xFF;
}

// #08: Formula 174 - scratchy
// Original: 32768 / ((t >> 256 | t + t * t) >> 123 - t % (t + t & 10))
// A=123 (shift base), B=10 (AND mask)
static inline uint8_t formula_scratchy(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    int32_t inner = shr(i, 256) | (i + i * i);
    int32_t shamt = a - safemod(i, (i + i) & b);
    return (uint8_t)safediv(32768, shr(inner, shamt)) & 0xFF;
}

// #09: Formula 175 - ambient techno
// Original: t * (t / (t * ((t >> t) / 600)) ^ 158) | t / 122
// A=600 (inner divisor), B=122 (bass divisor)
static inline uint8_t formula_amb_techno(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    int32_t inner = i * safediv(shr(i, i), a);
    return (uint8_t)((i * (safediv(i, inner) ^ 158)) | safediv(i, b)) & 0xFF;
}

// #10: Formula 176 - speccy
// Original: (t | t >> 1) / 101 & (99 % t ^ 23 % (t + t) + t) << 4
// = ((t|shr(t,1))/101) & (((99%t) ^ ((23%(t+t)) + t)) << 4)
// A=101 (divisor), B=99 (modulo base)
static inline uint8_t formula_speccy(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    int32_t left = safediv(i | shr(i, 1), a);
    int32_t right = (safemod(b, i) ^ (safemod(23, i + i) + i)) << 4;
    return (uint8_t)(left & right) & 0xFF;
}

// #11: Formula 177 - rising ring mod
// Original: ((28 << 153) % (127 + t) + t) / 187
// 28 << (153&31) = 28 << 25 = 939524096
// A=127 (offset), B=187 (divisor)
static inline uint8_t formula_ring_mod_up(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)safediv(safemod(939524096, a + i) + i, b) & 0xFF;
}

// #12: Formula 178 - square dance
// Original: (t*172 | t/112 | ((t&197|118)^t)*24000) + 300/105*32000 | (t+t)/(t-35)
// 300/105*32000 = 64000 (int arithmetic)
// A=172 (multiply), B=112 (divisor)
static inline uint8_t formula_sq_dance(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    int32_t left = (i * a) | safediv(i, b) | ((((i & 197) | 118) ^ i) * 24000);
    return (uint8_t)((left + 64000) | safediv(i + i, i - 35)) & 0xFF;
}

// #13: Formula 179 - harmonic seq
// Original: (t + 187) * 132 | ((t ^ t) >> 4096 << 53 << 61) + 100 + 16000 - t / 96
// t^t = 0, so middle term vanishes: (t+187)*132 | (16100 - t/96)
// A=132 (multiply), B=96 (divisor)
static inline uint8_t formula_harm_seq(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(((i + 187) * a) | (16100 - safediv(i, b))) & 0xFF;
}

// #14: Formula 180 - Phasing
// Original: t / (t - (43 ^ t)) >> (t | 43 + t) / 10000
// = shr(t/(t-(43^t)), ((t|(43+t))/10000))
// A=43 (XOR/add value), B=10000 (shift divisor)
static inline uint8_t formula_phasing(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)shr(safediv(i, i - (a ^ i)),
                        safediv(i | (a + i), b)) & 0xFF;
}

// #15: Formula 181 - sequencer
// Original: t * ((t >> 139) * 13 & 100) - 126
// t>>139 = t>>11 (139&31=11)
// A=13 (multiply), B=100 (AND mask)
static inline uint8_t formula_sequencer(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(i * ((shr(i, 139) * a) & b) - 126) & 0xFF;
}

// #16: Formula 182 - running
// Original: (15 | 100 + (184 / t - 40) - t / 96) & t * 44100 & t * 44100
// = (15 | (60 + 184/t - t/96)) & (t*44100)
// A=96 (divisor), B=184 (reverse div base)
static inline uint8_t formula_running(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)((15 | (60 + safediv(b, i) - safediv(i, a))) & (i * 44100)) & 0xFF;
}

// #17: Formula 184 - ring mod
// Original: (t ^ 78 - t) % (t << 182) + ((t | t) * 173 & t / 191)
// = safemod(t^(78-t), t<<22) + ((t*173) & (t/191))
// A=173 (multiply), B=191 (divisor)
static inline uint8_t formula_ring_mod(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(safemod(i ^ (78 - i), shl(i, 182))
                     + ((i * a) & safediv(i, b))) & 0xFF;
}

// #18: Formula 185 - death metal
// Original: (t % 123 * t * t >> t ^ t) % 65 - (t + t)
// = (shr((t%123)*t*t, t) ^ t) % 65 - (t+t)
// A=123 (modulo), B=65 (modulo)
static inline uint8_t formula_death_metal(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    int32_t inner = shr(safemod(i, a) * i * i, i) ^ i;
    return (uint8_t)(safemod(inner, b) - (i + i)) & 0xFF;
}

// #19: Formula 186 - crunchy alarm ending
// Original: 172 + t + ((59 ^ t + 151) << (t >> 84 % 24))
// 84%24=12, so shift = t>>12
// = 172 + t + shl(59^(t+151), shr(t, 12))
// A=59 (XOR base), B=151 (additive offset)
static inline uint8_t formula_crnch_alarm(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(172 + i + shl(a ^ (i + b), shr(i, 12))) & 0xFF;
}

// #20: Formula 187 - hollow alarm
// Original: ((t * (6 >> t) + t) * 46 | 174) & t * 120 >> 174
// = ((t*shr(6,t)+t)*46 | 174) & shr(t*120, 174)
// t>>174 = t>>14 (174&31=14)
// A=46 (multiply), B=120 (multiply)
static inline uint8_t formula_holw_alarm(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    return (uint8_t)(((i * shr(6, i) + i) * a | 174) & shr(i * b, 174)) & 0xFF;
}

// #21: Formula 188 - descending
// Original: 204 + (512 + (204 ^ t << 98 & (t - 31) / 142) % (t - 34))
// = 716 + safemod(204 ^ (shl(t,98) & (t-31)/142), t-34)
// t<<98 = t<<2 (98&31=2)
// A=142 (divisor), B=31 (offset)
static inline uint8_t formula_descending(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    int32_t inner = 204 ^ (shl(i, 98) & safediv(i - b, a));
    return (uint8_t)(716 + safemod(inner, i - 34)) & 0xFF;
}

// #22: Formula 189 - flittery
// Original: 124 * (t / 3000) & (121 * (t / 3000) & (68 * t ^ t << 17)) + (68 + 1000)
// = (124*d) & ((121*d & ((68*t) ^ shl(t,17))) + 1068)
// A=3000 (divisor/pattern length), B=68 (multiply/offset)
static inline uint8_t formula_flittery(uint32_t t, int32_t a, int32_t b) {
    int32_t i = (int32_t)t;
    int32_t d = safediv(i, a);
    return (uint8_t)((124 * d) & ((121 * d & ((b * i) ^ shl(i, 17))) + (b + 1000))) & 0xFF;
}

// Classic Viznut bytebeat (from "Experimental music from very short C programs",
// 2011): t*(((t>>12)|(t>>8))&(63&(t>>4))). Placeholder for as-yet-unfilled numbered
// slots. Ignores A/B. Base rate 8000 Hz.
static inline uint8_t formula_viznut(uint32_t t, int32_t /*a*/, int32_t /*b*/) {
    int32_t i = (int32_t)t;
    return (uint8_t)(i * ((shr(i, 12) | shr(i, 8)) & (63 & shr(i, 4)))) & 0xFF;
}

// #23: A440 tuning reference (daisy-vu3). Triangle wave; ignores A/B. Clocked at
// its base rate (28160 Hz) at 1x, one cycle = 64 t-samples -> 28160/64 = 440.0 Hz
// exactly. The engine forces rate=1x while this is Out1's formula, so the pitch is
// independent of the Rate knob / clock / V-oct. Displayed as function "99".
static inline uint8_t formula_ref_a440(uint32_t t, int32_t /*a*/, int32_t /*b*/) {
    uint32_t p = (t * 4u) & 0xFFu;                              // sawtooth, period 64
    return (uint8_t)(p < 128u ? (p * 2u) : ((255u - p) * 2u));  // fold to a triangle
}

// Formula access. There are 100 numbered slots (0..99) plus a special A440
// reference slot (index 100, displayed "AA"); GetFormulaCount() = 101. Numbered
// slots beyond the real (authored) formulas fall back to the Viznut placeholder.
const FormulaInfo* GetFormulaAt(int index);  // safe for 0..GetFormulaCount()-1
int GetFormulaCount();
int GetReferenceIndex();          // index of the A440 reference (the "AA" slot)
int GetNumberedSlotCount();       // count of numbered slots (100)
