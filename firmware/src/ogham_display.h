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

#pragma once
#include "tm1637.h"

class Display {
public:
    void Init(TM1637* tm);

    // Normal displays (skipped while a param flash is showing):
    //   "X-NN" = output X (1/2) playing numbered function NN (0-based, 00..99)
    //   "X-AA" = output X playing the A440 tuning reference (special slot)
    void ShowVoice(int outputNum, int functionNum);
    void ShowVoiceRef(int outputNum);   // "X-AA" A440 reference

    // Boot: firmware version as "M.mm" (e.g. " 1.00"); the major digit carries the DP.
    void ShowVersion(int major, int minor);

    // FX editor menu, fields in menu order. Field 0: "F.on"/"F.off" (global
    // on/off; state in `value`). Field 1: "F.Ser"/"F.Par" chain toggle. Param
    // fields (2..13): "T x. NN" where T = FX (C/F/P), x = sub (L level, t type,
    // a param1, b param2), 2nd-digit DP separates name from value NN (0-99).
    // Field 14: "O.En1".."O.dc2" CV-out mode. CV Out fields (15-17): "Sr.NN"
    // slew rise and "SF.NN" slew fall (independent, shared by every CV Out
    // mode) and "H.oFF"/"H.NNN" hold (DC modes only; value = the actual tick
    // count, 2-256). Field 18: internal LPG, consolidated on/off + decay --
    // "Lp.oF" off or "Lp.NN" on with that decay (value = the raw 0-99 field;
    // 0 means off). Field 19: "t.oFF"/"t.A"/"t.b" CV->Timbre routing. Field 20:
    // "q.oFF".."q.128" param-interp grid. Field 21: "d.on"/"d.oFF" Out2 drone.
    // blankValue (edit flash) blanks the value, keeps the label.
    void ShowFxEdit(int field, int value, bool parallel, bool blankValue);

    // Record a parameter value to flash (e.g., "A238" / "b123"). Cheap: it only
    // stores the pending value — the actual (blocking) TM1637 write is deferred to
    // DrawPendingFlash() at the display tick, so calling this every control-loop
    // iteration during a fast pot turn does NOT throttle the loop.
    void FlashParam(char prefix, int value);
    bool IsFlashing() const { return flashing_; }

    // Drop an active value flash straight away, so another interaction takes
    // the display now instead of queueing behind the timeout. The next display
    // tick (~17 ms) redraws whatever that interaction wants. Worth having
    // because the flash lingers for 2.5 s: without this, turning the encoder
    // while an A/B value is up appears to do nothing until the flash expires.
    void CancelFlash() { flashing_ = false; }

    // Flash a clock multiply/divide ratio (daisy-79d). `exp` is the power-of-two
    // exponent: >=0 multiply, <0 divide; magnitude shown = 2^|exp|. Multiply
    // shows just the number (e.g. "4", "32"); divide shows the number with the
    // TOP segment of the cell to its left lit -- a small bar implying division
    // (e.g. "‾8", "‾32"). Uses the same deferred-flash timing as FlashParam.
    void FlashClockRatio(int exp);

    // Rate knob flash (daisy-wvv). Two forms, because the knob means two
    // different things depending on the Mode switch.
    //
    // FlashSemitones: V/oct fine tune, in semitones off the centre detent, to
    // one decimal ("-1.5", " 0.0", "12.0", "-12.0"). Right-aligned with the
    // decimal point on the units cell, so the sign has room at full throw.
    void FlashSemitones(float semis);
    // Update a showing cents/multiplier flash WITHOUT restarting its timeout.
    // Needed because the pot is smoothed: when the knob stops, the value is
    // still converging, and the arming threshold is crossed only while it moves
    // fast. Without a refresh the display freezes at a mid-settle reading --
    // low if you arrived from the left, high from the right -- which looks
    // exactly like knob hysteresis and is not.
    void RefreshSemitones(float semis);

    // FlashRateMult: free-running playback rate as a multiplier, auto-ranged --
    // "  64" above ten, otherwise two decimals ("2.50", "1.00", "0.25", "0.02").
    void FlashRateMult(float mult);
    void RefreshRateMult(float mult);

    // Keep an already-showing param flash's value current (e.g. under CV) WITHOUT
    // restarting its timeout. No-op unless a flash for `prefix` is active — so a
    // CV-driven value change updates the number but can't hold the display on.
    void UpdateFlashValue(char prefix, int value);

    // Write the pending param flash (call at the ~30Hz display rate while flashing).
    void DrawPendingFlash();

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
    // How long the A/B (and clock-ratio) value lingers after the last knob
    // movement. Must outlast the gap between steps in a slow deliberate
    // adjustment, or the value blanks between one step and the next.
    static constexpr uint32_t FLASH_DURATION_MS = 2500;

    // Draw "[c0]-NN".
    void ShowLabeled(uint8_t c0seg, int twoDigit);

    // Segment builders shared by the Flash*/Refresh* pairs.
    static void BuildSemitones(float semis, uint8_t* s);
    static void BuildRateMult(float mult, uint8_t* s);
};
