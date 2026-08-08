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

#include "formulas.h"

static const FormulaInfo formulaTable[] = {
    {
        "bass throb",       // name
        "stevec64",         // author
        formula_bass_throb, // func
        8000,               // baseSampleRate
    },
    {
        "malfunction",      // name
        "stevec64",         // author
        formula_malfunction,
        8000,
    },
    {
        "running seq",      // name
        "stevec64",         // author
        formula_running_seq,
        8000,
    },
    {
        "basey seq",        // name
        "stevec64",         // author
        formula_basey_seq,
        8000,
    },
    {
        "morse code",       // name
        "stevec64",         // author
        formula_morse_code,
        8000,
    },
    {
        "noisy robot",      // name
        "stevec64",         // author
        formula_noisy_robot,
        8000,
    },
    {
        "harsh harm",       // name
        "stevec64",         // author
        formula_harsh_harm,
        8000,
    },
    {
        "scratchy",         // name
        "stevec64",         // author
        formula_scratchy,
        8000,
    },
    {
        "amb techno",       // name
        "stevec64",         // author
        formula_amb_techno,
        8000,
    },
    {
        "speccy",           // name
        "stevec64",         // author
        formula_speccy,
        8000,
    },
    {
        "ring mod up",      // name
        "stevec64",         // author
        formula_ring_mod_up,
        8000,
    },
    {
        "sq dance",         // name
        "stevec64",         // author
        formula_sq_dance,
        8000,
    },
    {
        "harm seq",         // name
        "stevec64",         // author
        formula_harm_seq,
        8000,
    },
    {
        "phasing",          // name
        "stevec64",         // author
        formula_phasing,
        8000,
    },
    {
        "sequencer",        // name
        "stevec64",         // author
        formula_sequencer,
        8000,
    },
    {
        "running",          // name
        "stevec64",         // author
        formula_running,
        8000,
    },
    {
        "ring mod",         // name
        "stevec64",         // author
        formula_ring_mod,
        8000,
    },
    {
        "death metal",      // name
        "stevec64",         // author
        formula_death_metal,
        8000,
    },
    {
        "crnch alarm",      // name
        "stevec64",         // author
        formula_crnch_alarm,
        8000,
    },
    {
        "holw alarm",       // name
        "stevec64",         // author
        formula_holw_alarm,
        8000,
    },
    {
        "descending",       // name
        "stevec64",         // author
        formula_descending,
        8000,
    },
    {
        "flittery",         // name
        "stevec64",         // author
        formula_flittery,
        8000,
    },
};

// Number of authored (real) formulas at the front of the numbered range.
static const int NUM_REAL = sizeof(formulaTable) / sizeof(formulaTable[0]);

// 100 numbered slots (0..99). Unfilled ones (NUM_REAL..99) play the Viznut
// placeholder until real formulas are authored into them.
static constexpr int NUM_NUMBERED = 100;

// Placeholder for unfilled numbered slots (classic Viznut).
static const FormulaInfo kViznutSlot = {
    "viznut", "viznut", formula_viznut, 8000,
};
// Special A440 tuning reference -- the "AA" slot (index 100, past the numbers).
static const FormulaInfo kRefA440Slot = {
    "ref A440", "keeos", formula_ref_a440,
    28154,      // tuned on hardware to read A=440.0Hz (28096 -> 439.1Hz; freq scales
                // linearly with base; corrects the real codec rate vs assumed 48kHz)
};

int GetFormulaCount()      { return NUM_NUMBERED + 1; }  // 100 numbered + AA
int GetReferenceIndex()    { return NUM_NUMBERED; }      // index 100 = "AA"
int GetNumberedSlotCount() { return NUM_NUMBERED; }

const FormulaInfo* GetFormulaAt(int index) {
    if (index == NUM_NUMBERED) return &kRefA440Slot;          // "AA" reference
    if (index >= 0 && index < NUM_REAL) return &formulaTable[index];
    return &kViznutSlot;                                      // unfilled numbered slot
}
