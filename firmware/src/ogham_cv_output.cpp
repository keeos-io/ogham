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

#include "ogham_cv_output.h"
#include <cmath>

void CvOutput::Init(daisy::DacHandle* dac) {
    dac_ = dac;
    dc_ = dc2_ = 0.0f;
    env_ = env2_ = 0.0f;
    envS_ = env2S_ = 0.0f;
}

// One envelope-follower step (DC-block -> full-wave rectify -> attack/release).
static inline void EnvStep(float in, float& dc, float& env,
                           float dcC, float atkC, float relC) {
    dc += dcC * (in - dc);
    float rect = fabsf(in - dc);
    if (rect > env) env += atkC * (rect - env);
    else            env += relC * (rect - env);
}

void CvOutput::ProcessSample(float out1Proc, float out2Proc, float raw1, float raw2) {
    // Two envelope followers (kept running in every mode so switching is instant),
    // each with an extra one-pole post-smoother for a gentler CV.
    EnvStep(out1Proc, dc_,  env_,  DC_COEFF, ATTACK_COEFF, RELEASE_COEFF);
    EnvStep(out2Proc, dc2_, env2_, DC_COEFF, ATTACK_COEFF, RELEASE_COEFF);
    envS_  += ENV_SMOOTH_COEFF * (env_  - envS_);
    env2S_ += ENV_SMOOTH_COEFF * (env2_ - env2S_);

    // Latch the raw voice samples for the DC modes (last of the block is written).
    raw1_ = raw1;
    raw2_ = raw2;
}

void CvOutput::UpdateOutput() {
    if (!dac_) return;

    float u;  // 0..1 DAC drive
    if (mode_ == Mode::EnvOut1 || mode_ == Mode::EnvOut2) {
        float env = (mode_ == Mode::EnvOut2) ? env2S_ : envS_;
        float v = env * ENV_GAIN;       // ~0..5 V envelope
        if (v < 0.0f) v = 0.0f;
        if (v > 5.0f) v = 5.0f;
        u = v / 5.0f;
    } else {
        // DC mode: raw voice sample (-1..1) -> full unipolar CV range (bytebeat LFO).
        float raw = (mode_ == Mode::DcOut2) ? raw2_ : raw1_;
        u = raw * 0.5f + 0.5f;          // -1..1 -> 0..1
    }
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;

    uint16_t out = (uint16_t)(u * 4095.0f + 0.5f);
    if (out > 4095) out = 4095;
    dac_->WriteValue(daisy::DacHandle::Channel::ONE, out);
}
