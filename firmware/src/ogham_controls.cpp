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

#include "ogham_controls.h"
#include <cmath>

// Scans of stillness before leftover sub-steps are discarded (150ms @ 20kHz).
static constexpr uint16_t ENC_IDLE_CLEAR_SCANS = 3000;


// Encoder direction (daisy-0wf). Two encoder batches exist with opposite A/B
// phase. The DEFAULT is the reversed/swapped-A/B part, which is what modules 2-5
// and everything built since actually use. Module 1 has the original encoder:
// build it with -DENC_REVERSED=0 (see Makefile.encorig, output
// ogham_bytebeat_encorig). Either way round, switching a unit over is just a
// matter of flashing the other build.
#ifndef ENC_REVERSED
#define ENC_REVERSED 1
#endif

void Controls::Init(daisy::DaisySeed* seed,
                    daisy::Encoder* encoder,
                    daisy::GateIn* gateIn,
                    daisy::GateIn* clockIn) {
    seed_ = seed;
    encoder_ = encoder;
    gateIn_ = gateIn;
    clockIn_ = clockIn;

    // Mode toggle input (Clock <-> V/oct): SPDT common -> GND, internal pull-up.
    modeGpio_.Init(seed_->GetPin(ogham::MODE_SW),
                   daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    voctSmoothed_ = 0.0f;
    voctMode_ = false;
    modeDebounce_ = 0;

    for (int i = 0; i < 4; i++) {
        potValues_[i] = 0.0f;
        potSmoothed_[i] = 0.0f;
    }
    cvSmoothed_[0] = CV_ZERO_OFFSET_A;
    cvSmoothed_[1] = CV_ZERO_OFFSET_B;
    combinedA_ = 0.5f;
    combinedB_ = 0.5f;
    encoderInc_ = 0;
    encoderPressed_ = false;
    encoderRising_ = false;
    lastEncoderPressed_ = false;

    // Direct A/B reads for the ISR-sampled quadrature decoder (SampleEncoder).
    // libDaisy's Encoder also owns these pins for the switch; a second INPUT+PULLUP
    // config is idempotent and reads are independent.
    encA_.Init(seed_->GetPin(ogham::ENC_A), daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    encB_.Init(seed_->GetPin(ogham::ENC_B), daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    encPrevState_ = (uint8_t)((encA_.Read() ? 2 : 0) | (encB_.Read() ? 1 : 0));
    encDelta_    = 0;
    encSubAccum_ = 0;

    gate_ = false;
    clock_ = false;
}

void Controls::PrimeSmoothing() {
    if (!seed_) return;
    for (int i = 0; i < 4; i++) potSmoothed_[i] = seed_->adc.GetFloat(i);
    cvSmoothed_[0] = seed_->adc.GetFloat(4);
    cvSmoothed_[1] = seed_->adc.GetFloat(5);
    voctSmoothed_ = seed_->adc.GetFloat(6);
    Process();  // recompute potValues_ / combinedA_ / combinedB_ from the snapped state
}

void Controls::SampleEncoder() {
    // Full-quadrature state machine. QDEC[(prev<<2)|cur] gives the sub-step (+1/-1
    // for a valid single-state transition, 0 for none/invalid). 4 sub-steps in one
    // direction = one detent. Bounce nets to ~0 and never reaches +-4, so it's
    // rejected. Runs on TIM5 at 20kHz, at an NVIC priority the audio callback
    // cannot starve -> immune to the main-loop display stall.
    static const int8_t QDEC[16] = {
         0, +1, -1,  0,
        -1,  0,  0, +1,
        +1,  0,  0, -1,
         0, -1, +1,  0,
    };
    uint8_t cur = (uint8_t)((encA_.Read() ? 2 : 0) | (encB_.Read() ? 1 : 0));
    const int8_t q = QDEC[(encPrevState_ << 2) | cur];
    encSubAccum_ += q;
    encPrevState_ = cur;
    if (encSubAccum_ >= 4)  { encDelta_++; encSubAccum_ -= 4; }
    if (encSubAccum_ <= -4) { encDelta_--; encSubAccum_ += 4; }

    // Residue clear. With the encoder parked at a detent, leftover sub-steps are
    // debris -- bounce that happened not to cancel, or the tail of a movement a
    // starved scan tore in half. Left alone, that debris is spent cancelling the
    // first sub-steps of the next click in the OPPOSITE direction, swallowing it
    // whole.
    //
    // Clear on STILLNESS, not on a direction reversal: ordinary contact bounce
    // IS a reversal (+1 immediately followed by -1) and must be allowed to
    // cancel -- 30% of transitions on a brisk turn are bounce pairs, against 4%
    // on a slow click. 150ms is far longer than the gap between sub-steps within
    // one detent even on a very deliberate turn, so this can never fire
    // mid-click.
    if (q != 0) {
        encIdleScans_ = 0;
    } else if (encSubAccum_ != 0 && ++encIdleScans_ >= ENC_IDLE_CLEAR_SCANS) {
        encSubAccum_ = 0;
        encIdleScans_ = 0;
    }
}

// Pot + CV, summed in software. See the comment at the call site for why the
// analog sum alone will not do.
//
//   pot  -> 0..1 across the pot's own full ADC travel
//   cv   -> (what the sum SHOULD read for this pot) - (what it does read),
//           normalised by `offset` so a given voltage moves the parameter by
//           the same amount it would have through the all-analog path
float Controls::CombineParam(float pot, float sumAdc, float offset, float gain) {
    float v = pot / POT_FULL_SCALE;
    if (v > 1.0f) v = 1.0f;

    // Clamped at 0: past the rail the expected reading is negative, and
    // subtracting a negative from a railed 0 would invent a CV that is not there.
    float expected = offset - gain * pot;
    if (expected < 0.0f) expected = 0.0f;
    v += (expected - sumAdc) / offset;

    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

void Controls::Process() {
    if (!seed_ || !encoder_) return;

    // Read and smooth the 4 pot ADC channels (ADC0-ADC3)
    for (int i = 0; i < 4; i++) {
        float raw = seed_->adc.GetFloat(i);
        float coeff = (i < 2) ? SMOOTH_COEFF_AB : SMOOTH_COEFF;
        potSmoothed_[i] += coeff * (raw - potSmoothed_[i]);
        potValues_[i] = potSmoothed_[i];
    }

    // Read and smooth MCP6004 outputs (ADC4-ADC5)
    // These already contain combined pot+CV+offset from hardware summing amp.
    // MCP6004 output: V_out = -V_cv - V_pot + 2.5V (clamped 0-3.3V)
    // High ADC = negative CV / low pot, Low ADC = positive CV / high pot
    for (int i = 0; i < 2; i++) {
        float raw = seed_->adc.GetFloat(4 + i);
        cvSmoothed_[i] += SMOOTH_COEFF_CV * (raw - cvSmoothed_[i]);
    }

    // V/oct input (ADC6): direct 0-3.3V tap (divider+clamp+U3D buffer), smoothed.
    voctSmoothed_ += SMOOTH_COEFF_CV * (seed_->adc.GetFloat(6) - voctSmoothed_);

    // Mode toggle (debounced): matches panel labelling — HIGH (switch floats to the
    // pull-up) = V/oct, LOW (switch -> GND) = Clock. (Polarity confirmed on hardware.)
    bool rawVoct = modeGpio_.Read();
    if (rawVoct != voctMode_) {
        if (++modeDebounce_ > 5) { voctMode_ = rawVoct; modeDebounce_ = 0; }
    } else {
        modeDebounce_ = 0;
    }

    // Pot and CV are summed in SOFTWARE, not taken from the analog sum.
    //
    // ADC4/5 carry the MCP6004 sum, and its gain is ~27% steeper than the pots'
    // range: with no CV patched the output reaches the 0 V rail at 79% of
    // rotation, so the last fifth of both A and B did nothing. Reading the sum
    // alone cannot recover that -- past the rail the output is 0 whatever the pot
    // does, and the information is gone.
    //
    // So the pot is taken from its OWN ADC (0/1), which sweeps cleanly across the
    // full rotation, and CV is recovered as the sum's departure from what the pot
    // alone would produce. Where the amp is railed that departure is unmeasurable,
    // which only costs a small negative CV at a high pot setting -- a region where
    // the parameter is already at or near maximum. A negative CV large enough to
    // bring the output back off the rail still reads correctly.
    combinedA_ = CombineParam(potValues_[0], cvSmoothed_[0],
                              CV_ZERO_OFFSET_A, POT_ADC_GAIN_A);
    combinedB_ = CombineParam(potValues_[1], cvSmoothed_[1],
                              CV_ZERO_OFFSET_B, POT_ADC_GAIN_B);

    // Encoder switch (still main-loop polled; a click spans many iterations).
    encoder_->Debounce();
    // Rotation comes from the ISR-sampled decoder (SampleEncoder on TIM5 at 20kHz),
    // NOT encoder_->Increment() -- so detents aren't dropped while the main loop is
    // stalled in a display write. Drain the accumulator atomically.
    int32_t d;
    __disable_irq();
    d = encDelta_;
    encDelta_ = 0;
    __enable_irq();
#if ENC_REVERSED
    encoderInc_ = (int)d;    // swapped-A/B encoder (default): CW increases
#else
    encoderInc_ = -(int)d;   // original encoder: negate so CW increases
#endif
    encoderPressed_ = encoder_->Pressed();
    encoderRising_ = encoderPressed_ && !lastEncoderPressed_;
    lastEncoderPressed_ = encoderPressed_;

    // Gate input (reset)
    gate_ = gateIn_->State();

    // Clock input
    clock_ = clockIn_->State();
}

float Controls::MapKnobToRate(float knob) {
    // Exponential mapping: 0.0 -> 1/64x, 0.5 -> 1.0x, 1.0 -> 64x (+-6 octaves).
    // Wide enough to run the engine at LFO-rate speeds for CV Out.
    return powf(2.0f, 12.0f * knob - 6.0f);
}
