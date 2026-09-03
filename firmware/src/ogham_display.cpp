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

#include "ogham_display.h"
#include "daisy_seed.h"

using namespace daisy;

void Display::Init(TM1637* tm) {
    tm_ = tm;
    flashing_ = false;
    flashStartMs_ = 0;
}

void Display::ShowVersion(int major, int minor) {
    if (!tm_) return;
    uint8_t s[4] = {
        0x00,                                                    // leading blank
        (uint8_t)(TM1637::Encode('0' + major % 10) | 0x80),      // major digit + DP
        TM1637::Encode('0' + (minor / 10) % 10),                 // minor tens
        TM1637::Encode('0' + minor % 10),                        // minor units
    };
    tm_->ShowChars(s);
}

void Display::ShowLabeled(uint8_t c0seg, int twoDigit) {
    if (!tm_ || flashing_) return;
    if (twoDigit < 0) twoDigit = 0;
    if (twoDigit > 99) twoDigit = 99;
    uint8_t segs[4];
    segs[0] = c0seg;
    segs[1] = TM1637::Encode('-');
    segs[2] = TM1637::Encode('0' + (twoDigit / 10) % 10);
    segs[3] = TM1637::Encode('0' + twoDigit % 10);
    tm_->ShowChars(segs);
}

void Display::ShowVoice(int outputNum, int functionNum) {
    ShowLabeled(TM1637::Encode('0' + outputNum), functionNum);
}

void Display::ShowVoiceRef(int outputNum) {
    if (!tm_ || flashing_) return;
    uint8_t segs[4];
    segs[0] = TM1637::Encode('0' + outputNum);
    segs[1] = TM1637::Encode('-');
    segs[2] = TM1637::Encode('A');
    segs[3] = TM1637::Encode('A');
    tm_->ShowChars(segs);
}

void Display::ShowFxEdit(int field, int value, bool parallel, bool blankValue) {
    if (!tm_ || flashing_) return;
    uint8_t segs[4];

    if (field == 0) {
        // Global FX on/off: "F.on" / "F.off" (first DP after the F).
        segs[0] = TM1637::Encode('F') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value)      { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('n'); segs[3] = 0x00; }
        else                 { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
    } else if (field == 1) {
        // Chain toggle: "F.Ser" / "F.Par" (first DP; 'F' for consistency with
        // the global on/off at field 0 -- both are chain-level, not per-stage).
        segs[0] = TM1637::Encode('F') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (parallel)   { segs[1] = TM1637::Encode('P'); segs[2] = TM1637::Encode('A'); segs[3] = TM1637::Encode('r'); }
        else                 { segs[1] = TM1637::Encode('5'); segs[2] = TM1637::Encode('E'); segs[3] = TM1637::Encode('r'); }  // '5' = S
    } else if (field == 14) {
        // CV-out mode (daisy-0pq): "O.En1"/"O.En2" (env followers) / "O.dc1"/"O.dc2" (DC).
        segs[0] = TM1637::Encode('0') | 0x80;   // 'O' -- no dedicated uppercase-O glyph;
                                                 // digit 0's full loop reads as O, not the
                                                 // smaller lowercase-o shape used in "oFF"
        if (blankValue)         { segs[1] = segs[2] = segs[3] = 0x00; }
        else {
            bool dc = (value >= 2);   // 0,1 = env; 2,3 = DC
            char n  = (value == 1 || value == 3) ? '2' : '1';
            segs[1] = TM1637::Encode(dc ? 'd' : 'E');
            segs[2] = TM1637::Encode(dc ? 'c' : 'n');
            segs[3] = TM1637::Encode(n);
        }
    } else if (field == 15) {
        // CV Out slew, rising: "Sr.NN" -- same name-DP-value shape as LPG decay
        // ('5' = S, no native S glyph on the segment font; matches field 1's
        // "SEr" precedent). Shared by every CV Out mode, not just the DC ones.
        // Applies when the target is above the current output.
        segs[0] = TM1637::Encode('5');
        segs[1] = TM1637::Encode('r') | 0x80;
        if (blankValue) { segs[2] = segs[3] = 0x00; }
        else {
            if (value < 0) value = 0;
            if (value > 99) value = 99;
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    } else if (field == 16) {
        // CV Out slew, falling: "SF.NN" -- same shape/scope as rise (field 15),
        // independent coefficient, applies when the target is below the
        // current output.
        segs[0] = TM1637::Encode('5');
        segs[1] = TM1637::Encode('F') | 0x80;
        if (blankValue) { segs[2] = segs[3] = 0x00; }
        else {
            if (value < 0) value = 0;
            if (value > 99) value = 99;
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    } else if (field == 17) {
        // CV Out hold: "H.oFF" or "H.002".."H.256" (first DP). `value` is the
        // actual tick count (0 = off), already resolved by the caller. DC
        // modes only.
        segs[0] = TM1637::Encode('H') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value <= 0) { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
        else {
            segs[1] = TM1637::Encode('0' + (value / 100) % 10);
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    } else if (field == 18) {
        // Internal LPG, consolidated on/off + decay: "Lp.oF" off, or
        // "Lp.01".."Lp.99" on with that decay (2ms..20s exp curve). Only 2
        // digits free for the value (vs. 3 on the single-char-label fields),
        // so "off" is abbreviated to "oF" rather than "oFF".
        segs[0] = TM1637::Encode('L');
        segs[1] = TM1637::Encode('p') | 0x80;
        if (blankValue)      { segs[2] = segs[3] = 0x00; }
        else if (value <= 0) { segs[2] = TM1637::Encode('o'); segs[3] = TM1637::Encode('F'); }
        else {
            if (value > 99) value = 99;
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    } else if (field == 19) {
        // CV->Timbre routing (daisy-gtw): "t.oFF" / "t.A" / "t.b" (first DP).
        segs[0] = TM1637::Encode('t') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value == 1) { segs[1] = TM1637::Encode('A'); segs[2] = 0x00; segs[3] = 0x00; }
        else if (value == 2) { segs[1] = TM1637::Encode('b'); segs[2] = 0x00; segs[3] = 0x00; }
        else                 { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
    } else if (field == 20) {
        // Param-interp grid: "q.oFF" or "q.016".."q.128" (first DP).
        segs[0] = TM1637::Encode('q') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value <= 0) { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
        else {
            segs[1] = TM1637::Encode('0' + (value / 100) % 10);
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    } else if (field == 21) {
        // Out2 decouple/drone: "d.on" / "d.oFF" (first DP; daisy-pcq).
        segs[0] = TM1637::Encode('d') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value)      { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('n'); segs[3] = 0x00; }
        else                 { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
    } else {
        // Param fields 2..13: "T x. NN" — digit0 = FX (C/F/P), digit1 = sub
        // (L/t/a/b) + separator DP. stage param field = 2 + stage*4 + sub.
        static const char fxLetter[3] = {'C', 'F', 'P'};
        static const char subGlyph[4] = {'L', 't', 'a', 'b'};
        if (field < 2) field = 2;
        if (field > 13) field = 13;
        int idx = field - 2;
        segs[0] = TM1637::Encode(fxLetter[idx / 4]);
        segs[1] = TM1637::Encode(subGlyph[idx % 4]) | 0x80;
        if (blankValue) { segs[2] = segs[3] = 0x00; }
        else {
            if (value < 0) value = 0;
            if (value > 99) value = 99;
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    }

    tm_->ShowChars(segs);
}

void Display::FlashParam(char prefix, int value) {
    // Only record the pending flash + (re)start the timeout. NO TM1637 write here:
    // the blocking write happens in DrawPendingFlash() at the display tick, so this
    // stays cheap even when called every loop iteration during a fast pot turn.
    pendingPrefix_ = prefix;
    pendingValue_  = value;
    flashRaw_      = false;
    flashing_      = true;
    flashStartMs_  = System::GetNow();
}

void Display::FlashClockRatio(int exp) {
    int mag = 1 << (exp < 0 ? -exp : exp);   // 2^|exp|
    uint8_t s[4] = {0, 0, 0, 0};
    s[3] = TM1637::Encode('0' + mag % 10);
    int leftIdx;                             // cell just left of the number's MSD
    if (mag >= 10) { s[2] = TM1637::Encode('0' + (mag / 10) % 10); leftIdx = 1; }
    else           { leftIdx = 2; }
    if (exp < 0) s[leftIdx] |= 0x01;         // top segment = divide hint
    for (int i = 0; i < 4; i++) pendingSegs_[i] = s[i];
    flashRaw_     = true;
    flashing_     = true;
    flashStartMs_ = System::GetNow();
}

void Display::BuildSemitones(float semis, uint8_t* s) {
    bool neg = semis < 0.0f;
    float mag = neg ? -semis : semis;
    int t = (int)(mag * 10.0f + 0.5f);      // tenths of a semitone
    if (t > 999) t = 999;
    s[0] = s[1] = 0x00;
    // Right-aligned: tenths in the last cell, units (carrying the point) next
    // to it, so "-12.0" still fits when the tens digit appears.
    s[3] = TM1637::Encode('0' + t % 10);
    s[2] = TM1637::Encode('0' + (t / 10) % 10) | 0x80;
    if (t >= 100) {
        s[1] = TM1637::Encode('0' + (t / 100) % 10);
        if (neg) s[0] = TM1637::Encode('-');
    } else if (neg) {
        s[1] = TM1637::Encode('-');
    }
}

void Display::BuildRateMult(float mult, uint8_t* s) {
    // Right-aligned throughout, with the decimal point on the units cell.
    //
    // Precision drops as the multiplier rises, because the mapping is
    // exponential and the digit has to be worth more than one ADC count or it
    // just dithers. Near the top of the clockwise half a single count moves the
    // multiplier by more than 0.01, so two decimals jittered on the last digit
    // with the knob sitting still. One decimal above 1x is quiet and still
    // finer than the ear. Below 1x two decimals are kept -- the whole
    // anticlockwise range is 0.02 to 1.00, and one decimal would collapse most
    // of it to "0.0".
    s[0] = s[1] = s[2] = s[3] = 0x00;
    if (mult >= 10.0f) {
        int v = (int)(mult + 0.5f);            // 64
        if (v > 99) v = 99;
        s[2] = TM1637::Encode('0' + (v / 10) % 10);
        s[3] = TM1637::Encode('0' + v % 10);
    } else if (mult >= 1.0f) {
        int v = (int)(mult * 10.0f + 0.5f);    // 1.0 -> 10, 2.5 -> 25
        if (v > 99) v = 99;
        s[2] = TM1637::Encode('0' + (v / 10) % 10) | 0x80;
        s[3] = TM1637::Encode('0' + v % 10);
    } else {
        int v = (int)(mult * 100.0f + 0.5f);   // 0.25 -> 25, 0.02 -> 2
        if (v > 99) v = 99;
        s[1] = TM1637::Encode('0') | 0x80;
        s[2] = TM1637::Encode('0' + (v / 10) % 10);
        s[3] = TM1637::Encode('0' + v % 10);
    }
}

void Display::FlashSemitones(float semis) {
    BuildSemitones(semis, pendingSegs_);
    flashRaw_     = true;
    flashing_     = true;
    flashStartMs_ = System::GetNow();
}

void Display::RefreshSemitones(float semis) {
    if (!flashing_ || !flashRaw_) return;
    BuildSemitones(semis, pendingSegs_);
}

void Display::FlashRateMult(float mult) {
    BuildRateMult(mult, pendingSegs_);
    flashRaw_     = true;
    flashing_     = true;
    flashStartMs_ = System::GetNow();
}

void Display::RefreshRateMult(float mult) {
    if (!flashing_ || !flashRaw_) return;
    BuildRateMult(mult, pendingSegs_);
}

void Display::UpdateFlashValue(char prefix, int value) {
    // Keep the live value current (incl. CV) during an active flash, but leave
    // flashStartMs_ alone so the timeout still expires after the last knob turn.
    if (!flashing_ || prefix != pendingPrefix_) return;
    pendingValue_ = value;
}

void Display::DrawPendingFlash() {
    if (!tm_ || !flashing_) return;
    if (flashRaw_) {
        uint8_t s[4] = { pendingSegs_[0], pendingSegs_[1], pendingSegs_[2], pendingSegs_[3] };
        tm_->ShowChars(s);
    } else {
        tm_->ShowPrefixNumber(pendingPrefix_, pendingValue_);
    }
}

void Display::Update() {
    if (!tm_) return;

    // Time out the param flash; the main loop redraws the normal display.
    if (flashing_) {
        uint32_t now = System::GetNow();
        if (now - flashStartMs_ >= FLASH_DURATION_MS) {
            flashing_ = false;
        }
    }
}
