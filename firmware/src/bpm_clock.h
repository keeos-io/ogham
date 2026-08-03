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
#include <cstdint>

class BpmClock {
public:
    void Init();

    // Called per sample from audio ISR — buffer audio + clock tick
    void ProcessSample(float sample);

    // Call when formula, A, or B changes. Starts a new estimation cycle.
    void RequestEstimate();

    // Call from main loop every iteration. Runs FFT frames + estimation + rate scaling.
    void Update(float rate);

    // For gate output (read from ISR or main loop)
    bool GetClockState() const { return clockHigh_; }

    // Current BPM for display (0 = not yet estimated)
    float GetBpm() const { return displayBpm_; }

    bool IsLocked() const { return locked_; }

private:
    // FFT configuration
    static constexpr int FFT_SIZE = 256;
    static constexpr int AUDIO_BUF_SIZE = 1024;  // Power of 2 for masking
    static constexpr int NUM_MAG_BINS = 127;      // FFT bins 1-127

    // Decimation to 100 Hz (every 480 samples at 48kHz)
    static constexpr uint32_t DECIM_FACTOR = 480;

    // Spectral flux buffer (4 seconds at 100 Hz)
    static constexpr int FLUX_BUF_SIZE = 400;

    // Clock pulse width
    static constexpr uint32_t PULSE_WIDTH = 480;  // 10ms at 48kHz

    // Autocorrelation limits
    static constexpr int MIN_LAG = 10;            // 100ms = 600 BPM max
    static constexpr int ESTIMATE_INTERVAL = 50;  // Retry every 0.5s of fresh data

    // Audio ring buffer (ISR writes, main reads)
    float audioBuffer_[AUDIO_BUF_SIZE];
    volatile int audioWritePos_;
    volatile int frameWritePos_;  // Snapshot at decimation boundary

    // Decimation
    uint32_t decimCounter_;
    volatile bool frameReady_;

    // FFT working buffer: 256 complex values, interleaved [re, im, re, im, ...]
    float fftBuffer_[FFT_SIZE * 2];

    // Magnitude spectra for spectral flux
    float curMag_[NUM_MAG_BINS];
    float prevMag_[NUM_MAG_BINS];
    bool hasPrevMag_;

    // Spectral flux ring buffer
    float fluxBuffer_[FLUX_BUF_SIZE];
    int fluxWritePos_;

    // Estimation state
    bool estimatePending_;
    volatile int freshSamples_;
    int lastRunSamples_;

    // BPM result
    float baseBpm_;
    float bpm_;
    float displayBpm_;
    bool locked_;

    // Clock generator (free-running)
    volatile uint32_t period_;
    uint32_t clockCounter_;
    bool clockHigh_;

    void ProcessFrame();
    void RunEstimate(float rate);
};
