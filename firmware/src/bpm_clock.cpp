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

#include "bpm_clock.h"
#include <cmath>
#include <cstring>

// Compact in-place radix-2 DIT FFT for N complex values.
// data[0..2N-1] = interleaved [re0, im0, re1, im1, ...]
static void fft_inplace(float* data, int N) {
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            float tr = data[2 * i];
            data[2 * i] = data[2 * j];
            data[2 * j] = tr;
            float ti = data[2 * i + 1];
            data[2 * i + 1] = data[2 * j + 1];
            data[2 * j + 1] = ti;
        }
    }

    // Butterfly stages
    for (int len = 2; len <= N; len *= 2) {
        float angle = -6.2831853f / (float)len;
        float wR = cosf(angle);
        float wI = sinf(angle);
        for (int i = 0; i < N; i += len) {
            float cR = 1.0f, cI = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                int u = 2 * (i + j);
                int v = 2 * (i + j + len / 2);
                float tR = cR * data[v] - cI * data[v + 1];
                float tI = cR * data[v + 1] + cI * data[v];
                data[v] = data[u] - tR;
                data[v + 1] = data[u + 1] - tI;
                data[u] += tR;
                data[u + 1] += tI;
                float nR = cR * wR - cI * wI;
                float nI = cR * wI + cI * wR;
                cR = nR;
                cI = nI;
            }
        }
    }
}

void BpmClock::Init() {
    memset(audioBuffer_, 0, sizeof(audioBuffer_));
    audioWritePos_ = 0;
    frameWritePos_ = 0;
    decimCounter_ = 0;
    frameReady_ = false;

    memset(curMag_, 0, sizeof(curMag_));
    memset(prevMag_, 0, sizeof(prevMag_));
    hasPrevMag_ = false;

    memset(fluxBuffer_, 0, sizeof(fluxBuffer_));
    fluxWritePos_ = 0;

    estimatePending_ = false;
    freshSamples_ = 0;
    lastRunSamples_ = 0;

    baseBpm_ = 0.0f;
    bpm_ = 0.0f;
    locked_ = false;

    period_ = 0;
    clockCounter_ = 0;
    clockHigh_ = false;
}

void BpmClock::RequestEstimate() {
    locked_ = false;
    estimatePending_ = true;
    freshSamples_ = 0;
    lastRunSamples_ = 0;
}

void BpmClock::ProcessSample(float sample) {
    // Store in audio ring buffer
    audioBuffer_[audioWritePos_] = sample;
    audioWritePos_ = (audioWritePos_ + 1) & (AUDIO_BUF_SIZE - 1);

    // Decimation to 100 Hz
    decimCounter_++;
    if (decimCounter_ >= DECIM_FACTOR) {
        decimCounter_ = 0;
        frameWritePos_ = audioWritePos_;  // Snapshot position for main loop
        frameReady_ = true;
        if (estimatePending_) {
            freshSamples_++;
        }
    }

    // Clock generator
    uint32_t p = period_;
    if (p > 0) {
        clockCounter_++;
        if (clockHigh_) {
            if (clockCounter_ >= PULSE_WIDTH) {
                clockHigh_ = false;
            }
        }
        if (clockCounter_ >= p) {
            clockCounter_ = 0;
            clockHigh_ = true;
        }
    } else {
        clockHigh_ = false;
    }
}

void BpmClock::Update(float rate) {
    // Process new spectral frame if ready
    if (frameReady_) {
        frameReady_ = false;
        ProcessFrame();
    }

    // Progressive estimation: try every 0.5s of fresh data until locked
    if (estimatePending_ && !locked_) {
        int fresh = freshSamples_;
        if (fresh >= ESTIMATE_INTERVAL &&
            fresh >= lastRunSamples_ + ESTIMATE_INTERVAL) {
            lastRunSamples_ = fresh;
            RunEstimate(rate);
        }
        if (fresh >= FLUX_BUF_SIZE && !locked_) {
            estimatePending_ = false;
        }
    }

    // Scale BPM and clock period to current rate
    if (baseBpm_ > 0.0f) {
        bpm_ = baseBpm_ * rate;
        float periodF = 48000.0f * 60.0f / bpm_;
        period_ = (uint32_t)(periodF + 0.5f);
    }
}

void BpmClock::ProcessFrame() {
    int wp = frameWritePos_;

    // Fill FFT buffer: windowed real samples as complex (imag = 0)
    for (int i = 0; i < FFT_SIZE; i++) {
        int idx = (wp - FFT_SIZE + i + AUDIO_BUF_SIZE) & (AUDIO_BUF_SIZE - 1);
        float w = 0.5f * (1.0f - cosf(6.2831853f * (float)i / (float)(FFT_SIZE - 1)));
        fftBuffer_[2 * i] = audioBuffer_[idx] * w;
        fftBuffer_[2 * i + 1] = 0.0f;
    }

    // 256-point complex FFT in-place
    fft_inplace(fftBuffer_, FFT_SIZE);

    // Compute magnitude squared for bins 1-127
    for (int k = 0; k < NUM_MAG_BINS; k++) {
        int bin = k + 1;
        float re = fftBuffer_[2 * bin];
        float im = fftBuffer_[2 * bin + 1];
        curMag_[k] = re * re + im * im;
    }

    // Compute spectral flux: sum of positive magnitude increases
    if (hasPrevMag_) {
        float flux = 0.0f;
        for (int i = 0; i < NUM_MAG_BINS; i++) {
            float diff = curMag_[i] - prevMag_[i];
            if (diff > 0.0f) {
                flux += diff;
            }
        }

        fluxBuffer_[fluxWritePos_] = flux;
        fluxWritePos_++;
        if (fluxWritePos_ >= FLUX_BUF_SIZE) {
            fluxWritePos_ = 0;
        }
    } else {
        hasPrevMag_ = true;
    }

    memcpy(prevMag_, curMag_, sizeof(curMag_));
}

void BpmClock::RunEstimate(float rate) {
    int bufLen = freshSamples_;
    if (bufLen > FLUX_BUF_SIZE) bufLen = FLUX_BUF_SIZE;

    int maxLag = bufLen / 2;
    if (maxLag < MIN_LAG) return;

    // Compute mean flux
    float mean = 0.0f;
    for (int i = 0; i < bufLen; i++) {
        int idx = (fluxWritePos_ - bufLen + i + FLUX_BUF_SIZE) % FLUX_BUF_SIZE;
        mean += fluxBuffer_[idx];
    }
    mean /= (float)bufLen;

    // Compute variance
    float variance = 0.0f;
    for (int i = 0; i < bufLen; i++) {
        int idx = (fluxWritePos_ - bufLen + i + FLUX_BUF_SIZE) % FLUX_BUF_SIZE;
        float d = fluxBuffer_[idx] - mean;
        variance += d * d;
    }
    variance /= (float)bufLen;

    if (variance < 1e-6f) return;

    // Mean-subtracted normalized autocorrelation
    int numLags = maxLag - MIN_LAG + 1;
    // Reuse fftBuffer_ as scratch for correlation (512 floats, enough for 200 lags)
    float* corr = fftBuffer_;

    for (int li = 0; li < numLags; li++) {
        int lag = MIN_LAG + li;
        float sum = 0.0f;
        int count = bufLen - lag;
        for (int i = 0; i < count; i++) {
            int idx1 = (fluxWritePos_ - bufLen + i + FLUX_BUF_SIZE) % FLUX_BUF_SIZE;
            int idx2 = (fluxWritePos_ - bufLen + i + lag + FLUX_BUF_SIZE) % FLUX_BUF_SIZE;
            sum += (fluxBuffer_[idx1] - mean) * (fluxBuffer_[idx2] - mean);
        }
        corr[li] = sum / ((float)count * variance);
    }

    // Find the first local maximum above confidence threshold
    int bestLag = 0;
    for (int li = 1; li < numLags - 1; li++) {
        if (corr[li] > corr[li - 1] && corr[li] >= corr[li + 1]) {
            if (corr[li] > 0.2f) {
                bestLag = MIN_LAG + li;
                break;
            }
        }
    }

    if (bestLag == 0) return;

    // Convert lag to BPM, normalize to 1x rate
    float beatPeriodSec = (float)bestLag / 100.0f;
    float rawBpm = 60.0f / beatPeriodSec;

    if (rate > 0.0f) {
        baseBpm_ = rawBpm / rate;
    } else {
        baseBpm_ = rawBpm;
    }

    // Fold into 40-200 BPM range by octave doubling/halving
    while (baseBpm_ > 200.0f) baseBpm_ *= 0.5f;
    while (baseBpm_ < 40.0f) baseBpm_ *= 2.0f;

    locked_ = true;
    estimatePending_ = false;
    bpm_ = baseBpm_ * rate;
    float periodF = 48000.0f * 60.0f / bpm_;
    period_ = (uint32_t)(periodF + 0.5f);
}
