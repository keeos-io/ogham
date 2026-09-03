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

// Ogham pot-calibration diagnostic (standalone firmware).
//
// Displays the RAW, smoothed ADC reading (0.000-1.000 shown as 0-999) of each of
// the four pots, using the SAME Controls smoothing the real firmware uses -- so a
// value read here is exactly what the firmware's calibration is compared against.
//
// The display auto-selects whichever pot you move:
//   A nnn = Param A pot        (linear; POT_ZERO 020 .. POT_FULL_SCALE 906)
//   b nnn = Param B pot        (linear; same span as A)
//   r nnn = Rate / Fine pot    (RATE_POT_CENTER 453, MIN 020, MAX 915)
//           NB the detent has ~12 counts of play -- park it from BOTH sides and
//           take the midpoint, not a single reading.
//   t nnn = Tone pot           (LOFI_CENTER 477, MIN 020, MAX 915)
//
// The MODE SWITCH picks the bank. Flipped the other way, the display shows the
// SLOPE of the summing amp, solved live:
//   c nnn = POT_ADC_GAIN for channel A, x1000   (999 = 0.999)
//   d nnn = POT_ADC_GAIN for channel B, x1000
// from adc = CV_ZERO_OFFSET - GAIN * pot, i.e. GAIN = (OFFSET - adc) / pot,
// using the intercept the firmware itself holds (Controls::CvZeroOffset).
//
// Solving it on the module rather than reporting two numbers to pair up by hand
// is the point: pot and summed channel are sampled in the same instant, so the
// answer does not depend on the knob holding still between two readings -- and
// it is INDEPENDENT OF KNOB POSITION, so nudging a pot to select its channel
// cannot corrupt the value being read.
//
// Park the pot anywhere from about a third to two thirds of the throw. Below
// that the division is dominated by noise; above it the amp rails at ground
// (around 78% of rotation) and there is no slope left to measure. Either way
// the display reads "---" rather than a wrong number.
//
// Encoder CLICK cycles the VIEW (peak-hold), shown by the decimal point:
//   LIVE  -> no dot        current position (park at 12 o'clock to read centre)
//   LO    -> dot on letter latched MINIMUM  ("A.458")   sweep fully CCW to capture
//   HI    -> dot on units  latched MAXIMUM  ("A458.")   sweep fully CW  to capture
// Min/Max latch from power-on; power-cycle / re-flash to reset them.
//
// Reflash the normal firmware afterwards:
//   openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
//     -c "program ./build/ogham_bytebeat.elf verify reset exit"

#include "daisy_seed.h"
#include "ogham_pins.h"
#include "ogham_controls.h"
#include "tm1637.h"

using namespace daisy;

static DaisySeed hw;
static Encoder   encoder;
static GateIn    gateIn;
static GateIn    clockIn;
static Controls  controls;
static TM1637    tm1637;

static const char POT_LETTER[6] = {'A', 'b', 'r', 't', 'c', 'd'};

// RAW channel value, used for movement detection and peak-hold: 0-3 are the
// pots (ADC0-3), 4-5 the MCP6004 summed channels (ADC4-5), all through the same
// smoothing the real firmware uses.
static float ChanValue(Controls& c, int i) {
    return (i < 4) ? c.GetPot(i) : c.GetCvRaw(i - 4);
}

// What actually gets DISPLAYED. Pots show their raw reading; the summed
// channels show the solved gain instead, which is why selection has to key off
// ChanValue above -- the gain is deliberately steady while the pot moves, so it
// would never trip a movement test.
static constexpr float GAIN_MIN_POT = 0.30f;   // below: division dominated by noise
static constexpr float GAIN_MIN_ADC = 0.02f;   // below: amp railed, no slope left
static bool DisplayValue(Controls& c, int i, float& out) {
    if (i < 4) { out = c.GetPot(i); return true; }
    float pot = c.GetPot(i - 4);
    float adc = c.GetCvRaw(i - 4);
    if (pot < GAIN_MIN_POT || adc < GAIN_MIN_ADC) return false;
    out = (Controls::CvZeroOffset(i - 4) - adc) / pot;
    return true;
}

// A pot must move by more than this (of full scale) to grab the display.
static constexpr float MOVE_THRESH = 0.008f;  // ~2 LSB of the 8-bit-ish reading
static constexpr uint32_t DRAW_MS  = 33;      // ~30 Hz display refresh

// view: 0 = LIVE, 1 = LO (min), 2 = HI (max)
static void ShowLetterValue(char letter, int value, int view) {
    if (value < 0)   value = 0;
    if (value > 999) value = 999;
    uint8_t s[4];
    s[0] = TM1637::Encode(letter);
    s[1] = TM1637::Encode('0' + (value / 100) % 10);   // always 3 digits (leading
    s[2] = TM1637::Encode('0' + (value / 10) % 10);    // zeros shown for clarity)
    s[3] = TM1637::Encode('0' + value % 10);
    if (view == 1) s[0] |= 0x80;   // LO: dot on the pot letter
    if (view == 2) s[3] |= 0x80;   // HI: dot on the last digit
    tm1637.ShowChars(s);
}

int main(void) {
    hw.Init();

    // Same 7-channel ADC layout as the main firmware, so Controls' indices line up.
    AdcChannelConfig adc[ogham::NUM_ADC_CHANNELS];
    adc[0].InitSingle(hw.GetPin(ogham::POT_A));
    adc[1].InitSingle(hw.GetPin(ogham::POT_B));
    adc[2].InitSingle(hw.GetPin(ogham::POT_RATE));
    adc[3].InitSingle(hw.GetPin(ogham::POT_LEVEL));
    adc[4].InitSingle(hw.GetPin(ogham::CV_A));
    adc[5].InitSingle(hw.GetPin(ogham::CV_B));
    adc[6].InitSingle(hw.GetPin(ogham::VOCT_ADC));
    hw.adc.Init(adc, ogham::NUM_ADC_CHANNELS);

    encoder.Init(hw.GetPin(ogham::ENC_A), hw.GetPin(ogham::ENC_B), hw.GetPin(ogham::ENC_SW));
    gateIn.Init(hw.GetPin(ogham::GATE_IN));
    clockIn.Init(hw.GetPin(ogham::CLK_IN));

    controls.Init(&hw, &encoder, &gateIn, &clockIn);
    tm1637.Init(&hw, ogham::TM1637_CLK, ogham::TM1637_DIO);

    hw.adc.Start();
    controls.PrimeSmoothing();

    // Splash "CAL " for ~1s so it's obvious this is the diagnostic, not the app.
    {
        uint8_t s[4] = { TM1637::Encode('C'), TM1637::Encode('A'), TM1637::Encode('L'), 0x00 };
        tm1637.ShowChars(s);
        uint32_t t0 = System::GetNow();
        while (System::GetNow() - t0 < 1000) { controls.Process(); System::Delay(1); }
    }

    int   sel  = 2;                // default to the Rate pot (the key centre-cal one)
    int   view = 0;                // 0 = LIVE, 1 = LO (min), 2 = HI (max)
    float lastRaw[6], potMin[6], potMax[6];
    for (int i = 0; i < 6; i++) {
        float p = ChanValue(controls, i);
        lastRaw[i] = potMin[i] = potMax[i] = p;
    }
    bool lastVoct = controls.IsVoctMode();
    uint32_t lastDraw = System::GetNow();

    while (1) {
        controls.Process();

        // Encoder click cycles the view: LIVE -> LO -> HI -> LIVE.
        if (controls.GetEncoderRisingEdge()) view = (view + 1) % 3;

        uint32_t now = System::GetNow();
        if (now - lastDraw >= DRAW_MS) {
            lastDraw = now;

            // Peak-hold every channel, but auto-select only within the bank the
            // mode switch has chosen -- moving pot A moves summed channel c by
            // almost exactly as much, so letting them compete would flip-flop.
            bool cvBank = controls.IsVoctMode();
            if (cvBank != lastVoct) {          // bank changed: land on its first
                sel = cvBank ? 4 : 2;          // channel rather than a stale index
                lastVoct = cvBank;
            }
            int lo = cvBank ? 4 : 0, hi = cvBank ? 6 : 4;
            int moved = -1; float best = MOVE_THRESH;
            for (int i = 0; i < 6; i++) {
                float p = ChanValue(controls, i);
                if (p < potMin[i]) potMin[i] = p;
                if (p > potMax[i]) potMax[i] = p;
                float d = p - lastRaw[i]; if (d < 0) d = -d;
                if (i >= lo && i < hi && d > best) { best = d; moved = i; }
                lastRaw[i] = p;
            }
            if (moved >= 0) sel = moved;

            // Gain channels ignore the peak-hold views: a min/max of a solved
            // slope is meaningless, and the live value is already stable.
            float val;
            if (sel >= 4) {
                if (DisplayValue(controls, sel, val)) {
                    ShowLetterValue(POT_LETTER[sel], (int)(val * 1000.0f + 0.5f), 0);
                } else {
                    uint8_t s[4] = { TM1637::Encode(POT_LETTER[sel]),
                                     TM1637::Encode('-'), TM1637::Encode('-'),
                                     TM1637::Encode('-') };
                    tm1637.ShowChars(s);
                }
            } else {
                val = (view == 1) ? potMin[sel]
                    : (view == 2) ? potMax[sel]
                                  : ChanValue(controls, sel);
                ShowLetterValue(POT_LETTER[sel], (int)(val * 1000.0f + 0.5f), view);
            }
        }

        System::Delay(1);
    }
}
