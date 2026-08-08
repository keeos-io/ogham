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
#include "tm1637.h"

class Display {
public:
    void Init(TM1637* tm);

    // Normal displays (skipped while a param flash is showing):
    //   "X-NN" = output X (1/2) playing numbered function NN (0-based, 00..99)
    //   "X-AA" = output X playing the A440 tuning reference (special slot)
    // dpClean lights the far-right decimal point when the lo-fi macro is clean.
    void ShowVoice(int outputNum, int functionNum, bool dpClean);
    void ShowVoiceRef(int outputNum, bool dpClean);   // "X-AA" A440 reference

    // Boot: firmware version as "M.mm" (e.g. " 1.00"); the major digit carries the DP.
    void ShowVersion(int major, int minor);

    // FX editor menu. Field 0: "F.on"/"F.off" (global on/off; state in `value`).
    // Param fields (1..12): "T x. NN" where T = FX (C/F/P), x = sub (L level,
    // t type, a param1, b param2), 2nd-digit DP separates name from value NN
    // (0-99). Chain field (13): "c.Ser"/"c.Par". LPG fields (18, 19):
    // "L.on"/"L.oFF" and "Ld.NN" decay. The far-right DP mirrors the
    // lo-fi clean-center indicator (dpClean). blankValue (edit flash) blanks
    // the value, keeps the label.
    void ShowFxEdit(int field, int value, bool parallel, bool dpClean, bool blankValue);

    // Record a parameter value to flash (e.g., "A238" / "b123"). Cheap: it only
    // stores the pending value — the actual (blocking) TM1637 write is deferred to
    // DrawPendingFlash() at the display tick, so calling this every control-loop
    // iteration during a fast pot turn does NOT throttle the loop.
    void FlashParam(char prefix, int value);
    bool IsFlashing() const { return flashing_; }

    // Flash a clock multiply/divide ratio (daisy-79d). `exp` is the power-of-two
    // exponent: >=0 multiply, <0 divide; magnitude shown = 2^|exp|. Multiply
    // shows just the number (e.g. "4", "32"); divide shows the number with the
    // TOP segment of the cell to its left lit -- a small bar implying division
    // (e.g. "‾8", "‾32"). Uses the same deferred-flash timing as FlashParam.
    void FlashClockRatio(int exp);

    // Keep an already-showing param flash's value current (e.g. under CV) WITHOUT
    // restarting its timeout. No-op unless a flash for `prefix` is active — so a
    // CV-driven value change updates the number but can't hold the display on.
    void UpdateFlashValue(char prefix, int value);

    // Write the pending param flash (call at the ~30Hz display rate while flashing).
    // dpClean lights the far-right decimal point (Lo-Fi clean-center indicator).
    void DrawPendingFlash(bool dpClean);

    // Call from main loop to handle flash timeout
    void Update();

private:
    TM1637* tm_ = nullptr;

    // Flash state
    bool flashing_ = false;
    uint32_t flashStartMs_ = 0;
    char pendingPrefix_ = ' ';
    int  pendingValue_ = 0;
    bool flashRaw_ = false;             // true: draw pendingSegs_ (ratio) not prefix+value
    uint8_t pendingSegs_[4] = {0,0,0,0};
    static constexpr uint32_t FLASH_DURATION_MS = 1000;

    // Draw "[c0]-NN" with digit-0 decimal point if dpClean.
    void ShowLabeled(uint8_t c0seg, int twoDigit, bool dpClean);
};
