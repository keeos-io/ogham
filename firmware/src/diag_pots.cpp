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

// Ogham pot-calibration diagnostic (standalone firmware).
//
// Displays the RAW, smoothed ADC reading (0.000-1.000 shown as 0-999) of each of
// the four pots, using the SAME Controls smoothing the real firmware uses -- so a
// value read here is exactly what the firmware's calibration is compared against.
//
// The display auto-selects whichever pot you move:
//   A nnn = Param A pot        (linear; no special centre)
//   b nnn = Param B pot        (linear; no special centre)
//   r nnn = Rate / Fine pot    (firmware RATE_POT_CENTER = 371, min ~0, max ~881)
//   t nnn = Tone pot           (firmware LOFI_CENTER = 446, full CW POT_MAX ~960)
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

static const char POT_LETTER[4] = {'A', 'b', 'r', 't'};

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
    float lastRaw[4], potMin[4], potMax[4];
    for (int i = 0; i < 4; i++) {
        float p = controls.GetPot(i);
        lastRaw[i] = potMin[i] = potMax[i] = p;
    }
    uint32_t lastDraw = System::GetNow();

    while (1) {
        controls.Process();

        // Encoder click cycles the view: LIVE -> LO -> HI -> LIVE.
        if (controls.GetEncoderRisingEdge()) view = (view + 1) % 3;

        uint32_t now = System::GetNow();
        if (now - lastDraw >= DRAW_MS) {
            lastDraw = now;

            // Update peak-hold + auto-select whichever pot moved the most.
            int moved = -1; float best = MOVE_THRESH;
            for (int i = 0; i < 4; i++) {
                float p = controls.GetPot(i);
                if (p < potMin[i]) potMin[i] = p;
                if (p > potMax[i]) potMax[i] = p;
                float d = p - lastRaw[i]; if (d < 0) d = -d;
                if (d > best) { best = d; moved = i; }
                lastRaw[i] = p;
            }
            if (moved >= 0) sel = moved;

            float val = (view == 1) ? potMin[sel]
                      : (view == 2) ? potMax[sel]
                                    : controls.GetPot(sel);
            ShowLetterValue(POT_LETTER[sel], (int)(val * 1000.0f + 0.5f), view);
        }

        System::Delay(1);
    }
}
