#include "formulas.h"

static const FormulaInfo formulaTable[] = {
    {
        "bass throb",       // name
        "stevec64",         // author
        formula_bass_throb, // func
        8000,               // baseSampleRate
        238, 123,           // defaultA, defaultB
        100, 255,           // minA, maxA
        50, 200,            // minB, maxB
    },
    {
        "malfunction",      // name
        "stevec64",         // author
        formula_malfunction,
        8000,
        15, 112,            // defaultA, defaultB
        5, 30,              // minA, maxA
        50, 200,            // minB, maxB
    },
    {
        "running seq",      // name
        "stevec64",         // author
        formula_running_seq,
        8000,
        123, 159,           // defaultA, defaultB
        50, 200,            // minA, maxA
        80, 255,            // minB, maxB
    },
    {
        "basey seq",        // name
        "stevec64",         // author
        formula_basey_seq,
        8000,
        123, 133,           // defaultA, defaultB
        50, 200,            // minA, maxA
        50, 255,            // minB, maxB
    },
    {
        "morse code",       // name
        "stevec64",         // author
        formula_morse_code,
        8000,
        131, 500,           // defaultA, defaultB
        50, 255,            // minA, maxA
        200, 1000,          // minB, maxB
    },
    {
        "noisy robot",      // name
        "stevec64",         // author
        formula_noisy_robot,
        8000,
        3000, 193,          // defaultA, defaultB
        1000, 8000,         // minA, maxA
        100, 255,           // minB, maxB
    },
    {
        "harsh harm",       // name
        "stevec64",         // author
        formula_harsh_harm,
        8000,
        155, 122,           // defaultA, defaultB
        50, 255,            // minA, maxA
        50, 200,            // minB, maxB
    },
    {
        "scratchy",         // name
        "stevec64",         // author
        formula_scratchy,
        8000,
        123, 10,            // defaultA, defaultB
        50, 200,            // minA, maxA
        2, 20,              // minB, maxB
    },
    {
        "amb techno",       // name
        "stevec64",         // author
        formula_amb_techno,
        8000,
        600, 122,           // defaultA, defaultB
        200, 1200,          // minA, maxA
        50, 200,            // minB, maxB
    },
    {
        "speccy",           // name
        "stevec64",         // author
        formula_speccy,
        8000,
        101, 99,            // defaultA, defaultB
        30, 200,            // minA, maxA
        30, 200,            // minB, maxB
    },
    {
        "ring mod up",      // name
        "stevec64",         // author
        formula_ring_mod_up,
        8000,
        127, 187,           // defaultA, defaultB
        50, 255,            // minA, maxA
        80, 300,            // minB, maxB
    },
    {
        "sq dance",         // name
        "stevec64",         // author
        formula_sq_dance,
        8000,
        172, 112,           // defaultA, defaultB
        80, 255,            // minA, maxA
        50, 200,            // minB, maxB
    },
    {
        "harm seq",         // name
        "stevec64",         // author
        formula_harm_seq,
        8000,
        132, 96,            // defaultA, defaultB
        50, 255,            // minA, maxA
        40, 200,            // minB, maxB
    },
    {
        "phasing",          // name
        "stevec64",         // author
        formula_phasing,
        8000,
        43, 10000,          // defaultA, defaultB
        10, 100,            // minA, maxA
        5000, 20000,        // minB, maxB
    },
    {
        "sequencer",        // name
        "stevec64",         // author
        formula_sequencer,
        8000,
        13, 100,            // defaultA, defaultB
        1, 30,              // minA, maxA
        30, 200,            // minB, maxB
    },
    {
        "running",          // name
        "stevec64",         // author
        formula_running,
        8000,
        96, 184,            // defaultA, defaultB
        40, 200,            // minA, maxA
        80, 255,            // minB, maxB
    },
    {
        "ring mod",         // name
        "stevec64",         // author
        formula_ring_mod,
        8000,
        173, 191,           // defaultA, defaultB
        80, 255,            // minA, maxA
        80, 300,            // minB, maxB
    },
    {
        "death metal",      // name
        "stevec64",         // author
        formula_death_metal,
        8000,
        123, 65,            // defaultA, defaultB
        50, 255,            // minA, maxA
        20, 130,            // minB, maxB
    },
    {
        "crnch alarm",      // name
        "stevec64",         // author
        formula_crnch_alarm,
        8000,
        59, 151,            // defaultA, defaultB
        20, 120,            // minA, maxA
        50, 255,            // minB, maxB
    },
    {
        "holw alarm",       // name
        "stevec64",         // author
        formula_holw_alarm,
        8000,
        46, 120,            // defaultA, defaultB
        10, 100,            // minA, maxA
        50, 200,            // minB, maxB
    },
    {
        "descending",       // name
        "stevec64",         // author
        formula_descending,
        8000,
        142, 31,            // defaultA, defaultB
        50, 255,            // minA, maxA
        10, 60,             // minB, maxB
    },
    {
        "flittery",         // name
        "stevec64",         // author
        formula_flittery,
        8000,
        3000, 68,           // defaultA, defaultB
        1000, 8000,         // minA, maxA
        20, 150,            // minB, maxB
    },
};

// Number of authored (real) formulas at the front of the numbered range.
static const int NUM_REAL = sizeof(formulaTable) / sizeof(formulaTable[0]);

// 100 numbered slots (0..99). Unfilled ones (NUM_REAL..99) play the Viznut
// placeholder until real formulas are authored into them.
static constexpr int NUM_NUMBERED = 100;

// Placeholder for unfilled numbered slots (classic Viznut).
static const FormulaInfo kViznutSlot = {
    "viznut", "viznut", formula_viznut, 8000, 0, 0, 0, 0, 0, 0,
};
// Special A440 tuning reference -- the "AA" slot (index 100, past the numbers).
static const FormulaInfo kRefA440Slot = {
    "ref A440", "keeos", formula_ref_a440,
    28154,      // tuned on hardware to read A=440.0Hz (28096 -> 439.1Hz; freq scales
                // linearly with base; corrects the real codec rate vs assumed 48kHz)
    0, 0, 0, 0, 0, 0,
};

int GetFormulaCount()      { return NUM_NUMBERED + 1; }  // 100 numbered + AA
int GetReferenceIndex()    { return NUM_NUMBERED; }      // index 100 = "AA"
int GetNumberedSlotCount() { return NUM_NUMBERED; }

const FormulaInfo* GetFormulaAt(int index) {
    if (index == NUM_NUMBERED) return &kRefA440Slot;          // "AA" reference
    if (index >= 0 && index < NUM_REAL) return &formulaTable[index];
    return &kViznutSlot;                                      // unfilled numbered slot
}
