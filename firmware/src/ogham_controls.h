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
#include "daisy_seed.h"
#include "ogham_pins.h"

class Controls {
public:
    // Initialize with Seed hardware + pre-configured encoder and gate
    void Init(daisy::DaisySeed* seed,
              daisy::Encoder* encoder,
              daisy::GateIn* gateIn,
              daisy::GateIn* clockIn);

    // Call once per main loop iteration (~1kHz)
    void Process();

    // Snap the smoothing filters to the current raw ADC (no ramp) so readings are
    // immediately stable at boot. Call after the ADC has valid samples.
    void PrimeSmoothing();

    // Combined pot+CV for A and B (0.0 to 1.0), from MCP6004 output
    // The MCP6004 inverting summing amp combines pot + CV + offset in hardware.
    // ADC4/5 already contain the combined value; firmware just inverts and scales.
    float GetCombinedA() const { return combinedA_; }
    float GetCombinedB() const { return combinedB_; }

    // Rate pot (0.0 to 1.0) — direct ADC, no CV input
    float GetRate() const { return potValues_[2]; }

    // Level pot (0.0 to 1.0) — direct ADC, no CV input
    float GetLevel() const { return potValues_[3]; }

    // V/oct input (0.0 to 1.0) — ADC6, analog tap on the Clock jack (Seed pin 28)
    float GetVoct() const { return voctSmoothed_; }

    // Mode toggle: true = V/oct (pitch) mode, false = Clock (tempo) mode
    bool IsVoctMode() const { return voctMode_; }

    // Encoder
    int GetEncoderIncrement() const { return encoderInc_; }
    bool GetEncoderPressed() const { return encoderPressed_; }
    bool GetEncoderRisingEdge() const { return encoderRising_; }

    // Sample the rotary A/B quadrature and accumulate detents. Called from a
    // timer ISR (TIM5, see ogham_main.cpp) so encoder steps are never dropped
    // during the blocking ~9ms TM1637 display writes that stall the main loop.
    void SampleEncoder();

    // --- Telemetry accessors (for the PC monitor) ---
    float GetPot(int i) const { return (i >= 0 && i < 4) ? potValues_[i] : 0.0f; }     // raw ADC0-3

    // Pot + CV summed in software (the analog sum rails at 79% of pot travel).
    static float CombineParam(float pot, float sumAdc, float offset, float gain);
    float GetCvRaw(int i) const { return (i >= 0 && i < 2) ? cvSmoothed_[i] : 0.0f; }  // raw ADC4-5

    // The summing-amp intercept for channel 0/1, exposed so the calibration
    // diagnostic fits against the same number the firmware uses rather than a
    // copy of it that can drift.
    static constexpr float CvZeroOffset(int ch) {
        return ch ? CV_ZERO_OFFSET_B : CV_ZERO_OFFSET_A;
    }
    bool GetGate() const { return gate_; }
    bool GetClock() const { return clock_; }

    // Map a knob value (0-1) to exponential rate (1/64x - 64x)
    static float MapKnobToRate(float knob);

private:
    daisy::DaisySeed* seed_ = nullptr;
    daisy::Encoder* encoder_ = nullptr;
    daisy::GateIn* gateIn_ = nullptr;
    daisy::GateIn* clockIn_ = nullptr;

    // Smoothed pot values (indices: 0=A, 1=B, 2=Rate, 3=Level)
    // ADC0/1 are pot-only (for display), ADC2/3 are Rate/Level
    float potValues_[4] = {};
    float potSmoothed_[4] = {};

    // Smoothed MCP6004 output (combined pot+CV+offset, inverted)
    float cvSmoothed_[2] = {};

    // V/oct input (ADC6) + Clock/V-oct mode toggle (GPIO, debounced)
    float voctSmoothed_ = 0.0f;
    daisy::GPIO modeGpio_;
    bool voctMode_ = false;
    int  modeDebounce_ = 0;

    // Combined pot+CV (derived from MCP6004 output on ADC4/5)
    float combinedA_ = 0.0f;
    float combinedB_ = 0.0f;

    // Encoder state
    int encoderInc_ = 0;
    bool encoderPressed_ = false;
    bool encoderRising_ = false;
    bool lastEncoderPressed_ = false;

    // Rotary A/B decoded in the scan ISR (miss-proof vs the display blocking).
    // Full-quadrature state machine: 4 sub-steps = 1 detent.
    daisy::GPIO      encA_, encB_;   // direct pin reads for the ISR decoder
    volatile int32_t encDelta_ = 0;  // detents accumulated by the ISR, drained by Process()
    int8_t           encSubAccum_ = 0;
    uint8_t          encPrevState_ = 0;  // last (A<<1)|B
    uint16_t         encIdleScans_ = 0;  // scans since the last sub-step

    // Gate state
    bool gate_ = false;

    // Clock state
    bool clock_ = false;

    static constexpr float SMOOTH_COEFF = 0.01f;
    static constexpr float SMOOTH_COEFF_AB = 0.003f;  // Heavier filtering for A/B
    static constexpr float SMOOTH_COEFF_CV = 0.005f;  // MCP6004 output smoothing

    // MCP6004 inverting summing amplifier output characteristics:
    //   V_out = -V_cv - V_pot + 2.5V  (clamped to 0-3.3V by rail-to-rail op-amp)
    //   As ADC float: adc = V_out / 3.3V
    //   At 0V CV, pot=0V:  adc = 2.5/3.3 ≈ 0.758
    //   At +5V CV, pot=0V: adc = 0.0 (clamped)
    //   At -5V CV, pot=0V: adc = 1.0 (clamped at 3.3V rail)
    //
    // To recover a 0-1 parameter value: invert and normalize.
    //
    // ADC4/5 read the MCP6004 sum of pot and CV, inverted: adc = OFFSET - GAIN*pot
    // with no CV patched. Both channels are measured, not assumed -- a nominal
    // 0.758 offset is about 1% out and shows up as a scale error between A and B.
    // Fitted over a full sweep (236 samples, residual < 0.006, i.e. linear):
    //
    //     A:  adc = 0.7501 - 0.9990 * pot      B:  adc = 0.7499 - 0.9984 * pot
    //
    // Re-measured on the 2026-09 standard part: the intercept moved to 0.734 (A)
    // and 0.736 (B), about 0.015 below the old fit. That is what stopped A and B
    // reaching 0 -- with the knob against the anticlockwise stop the pot term is
    // exactly zero, so the whole residual was this offset being read as a CV
    // nobody had patched, and at 0.0196 and 0.0157 in parameter units it was
    // wider than the null band could swallow.
    //
    // Correcting the intercept fixes the rail exactly, whatever the slope does:
    // the recovered CV error scales with pot position, so at pot = 0 it is zero
    // by construction.
    //
    // The SLOPE was then measured separately and had NOT moved. The diag
    // firmware solves GAIN = (OFFSET - adc) / pot on the module, sampling knob
    // and summed channel in the same instant, and read 0.999 on both channels.
    // A matches its stored 0.9990 exactly; B's stored 0.9984 sits inside the
    // reading's own 0.001 resolution, so both are left alone -- the originals
    // come from a 236-sample fit and are the better numbers. A single 3-digit
    // reading confirms them rather than improving on them.
    //
    // (The slope cannot be had from a full-clockwise sweep: the summing amp
    // rails at ground around 78% of rotation, so that reading is a clamp at 0,
    // not a data point. Hence solving it mid-travel.)
    static constexpr float CV_ZERO_OFFSET_A = 0.734f;   // measured, standard part
    static constexpr float CV_ZERO_OFFSET_B = 0.736f;   // measured, standard part

    // Slope of that line: how much the summed channel moves per unit of pot.
    // The pots reach 0.9508, so the amp would need a gain of 0.789 to arrive at
    // the rail exactly at full rotation. At ~1.0 it gets there at 0.751 -- 79% of
    // travel -- and the last fifth of the rotation is dead. That is a hardware
    // gain choice; the firmware works around it by reading the pot directly.
    static constexpr float POT_ADC_GAIN_A = 0.9990f;
    static constexpr float POT_ADC_GAIN_B = 0.9984f;

    // A/B pot span, raw ADC0/1. Both ends are deliberately set INSIDE the
    // measured travel so the parameter reaches 0 and 255 with room to spare.
    // Reaching the rails is what matters; using every last degree of rotation
    // is not, and a margin is what makes the build tolerant of a pot batch that
    // lands somewhere else.
    //
    // Measured full clockwise: 0.936 on the 2026-09 standard part, 0.951 on the
    // earlier fleet (modules 1-5) -- a 0.015 spread between batches. The top
    // margin is 0.030, twice that spread; the bottom is 0.020, pure insurance
    // since every pot measured so far bottoms out at exactly 0.000. Both
    // batches clear the rails: v = 1.034 on the new part, 1.051 on the old, so
    // one calibration serves the whole fleet.
    //
    // Cost is 3.2% of the throw at the top and 2.1% at the bottom already
    // sitting at the limit -- about ten and six degrees of a 300-degree pot.
    static constexpr float POT_ZERO       = 0.020f;
    static constexpr float POT_FULL_SCALE = 0.906f;

    // Null band on the recovered CV, in parameter units (1.0 = full scale).
    // A departure smaller than this reads as no CV at all, which is what keeps
    // a mis-fitted intercept from holding A and B off their rails.
    //
    // Sized from the intercept spread actually measured, not guessed. Three
    // units span 0.7340 to 0.7501 ADC -- 0.0215 in parameter units, 5.5 counts
    // of 255. The band was 0.012 (3.1 counts), SMALLER than that spread, which
    // is exactly why the 2026-09 standard part bottomed out at 5 and 4 instead
    // of 0 even though its pot reached ground cleanly. 0.030 covers 7.6 counts,
    // about 1.4x the observed spread, so the next module off the bench should
    // reach both rails without needing its own intercept measured.
    //
    // The cost is a ~74 mV dead zone around 0 V on the CV inputs: a modulation
    // smaller than that does nothing. That is under 3% of their range and
    // inside the noise of most CV sources. It does not touch the knob rails --
    // those come from the pot term, which is exact at both ends.
    //
    // If a future batch ever spreads wider than this, the answer is per-unit
    // calibration (measure the intercept with the diag firmware and store it)
    // rather than widening the band again -- past roughly 0.04 the dead zone
    // starts being audible as a step when a slow CV crosses it.
    static constexpr float CV_NULL_BAND = 0.030f;
};
