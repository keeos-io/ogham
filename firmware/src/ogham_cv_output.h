#pragma once
#include "daisy_seed.h"

// CV Out on the Daisy internal DAC channel ONE (-> TL072 gain stage -> CV-out jack).
// Mode-selectable (daisy-0pq):
//   EnvOut1 - amplitude envelope follower of Out1 (DC-block -> rectify -> A/R smooth)
//   EnvOut2 - amplitude envelope follower of Out2
//   DcOut1  - raw Out1 formula sample as a DC voltage (bytebeat-as-CV/LFO)
//   DcOut2  - raw Out2 formula sample as a DC voltage
// The DC modes take the pre-Lo-Fi/FX voice sample so the CV shape is the bytebeat
// itself, and can hold slow/DC values the AC-coupled audio outs cannot.
class CvOutput {
public:
    enum class Mode : uint8_t { EnvOut1 = 0, EnvOut2 = 1, DcOut1 = 2, DcOut2 = 3 };

    void Init(daisy::DacHandle* dac);
    void SetMode(Mode m) { mode_ = m; }

    // Process one audio sample (called from the audio ISR, 48 kHz). out1Proc/out2Proc
    // feed the two envelope followers; raw1/raw2 are the pre-Lo-Fi voices for DC modes.
    void ProcessSample(float out1Proc, float out2Proc, float raw1, float raw2);

    // Write the CV (called from the main loop)
    void UpdateOutput();

    float GetEnv() const { return envS_; }  // 0..~1, for telemetry/tuning

private:
    daisy::DacHandle* dac_ = nullptr;
    Mode  mode_ = Mode::EnvOut1;

    // Two envelope followers (Out1, Out2): each DC-block -> rectify -> attack/release,
    // then an extra one-pole smoother (envS_/env2S_) for a gentler CV.
    float dc_    = 0.0f, dc2_   = 0.0f;   // slow DC estimates (DC blockers)
    float env_   = 0.0f, env2_  = 0.0f;   // rectified A/R envelopes (0..~1)
    float envS_  = 0.0f, env2S_ = 0.0f;   // post-smoothed envelopes (output)
    float raw1_  = 0.0f, raw2_  = 0.0f;   // latest raw voice samples (-1..1) for DC modes

    // Tuning (one-pole coeffs at 48 kHz). c -> time constant ~1/(c*48000) s.
    static constexpr float DC_COEFF      = 0.0008f;  // ~6 Hz DC blocker
    static constexpr float ATTACK_COEFF  = 0.0154f;  // ~1.4 ms rise
    static constexpr float RELEASE_COEFF = 0.000615f; // ~34 ms fall
    static constexpr float ENV_SMOOTH_COEFF = 0.0020f; // ~10 ms post-smoothing (extra)
    static constexpr float ENV_GAIN      = 6.0f;     // env(0..1) -> 0..5 V (clamped)
};
