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
    static constexpr float CV_ZERO_OFFSET_A = 0.7501f;
    static constexpr float CV_ZERO_OFFSET_B = 0.7499f;

    // Slope of that line: how much the summed channel moves per unit of pot.
    // The pots reach 0.9508, so the amp would need a gain of 0.789 to arrive at
    // the rail exactly at full rotation. At ~1.0 it gets there at 0.751 -- 79% of
    // travel -- and the last fifth of the rotation is dead. That is a hardware
    // gain choice; the firmware works around it by reading the pot directly.
    static constexpr float POT_ADC_GAIN_A = 0.9990f;
    static constexpr float POT_ADC_GAIN_B = 0.9984f;

    // Raw ADC0/1 at full clockwise. Deliberately a little under the measured
    // 0.9508 so every module reaches 255 before the end stop rather than falling
    // a count or two short; the cost is that the top ~0.6% of travel is already
    // at maximum. VERIFY ON A SECOND MODULE before trusting the margin.
    static constexpr float POT_FULL_SCALE = 0.9450f;
};
