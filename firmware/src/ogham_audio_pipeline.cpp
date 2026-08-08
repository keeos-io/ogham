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

#include "ogham_audio_pipeline.h"
#include <cmath>

// Live-tunable Lo-Fi config (RAM; the monitor writes this over SWD). Defaults
// here are the starting point; tweak live via the monitor, then bake in.
LofiConfig g_lofiConfig = {
    0x4C4F4649u,  // 'LOFI'
    740.0f,       // bpCutoffStartHz (just past center)
    4100.0f,      // bpCutoffEndHz   (full CW)
    2.9f,         // bpQStart  (a touch resonant)
    6.9f,         // bpQEnd    (narrow/resonant)
    0.78f,        // bpMixMax  (wet blend at full CW)
    1.3f,         // sweepCurve (>1 back-loads the band-pass sweep so most of the cutoff+mix
                  //             change lands in the upper throw; was 0.40 = front-loaded/maxed by ~4 o'clock)
};

// Lo-fi macro tuning
namespace {
    // Measured 12-o'clock ADC value for POT_LEVEL (recalibrated 2026-07-23 via
    // read_once.py at true noon; the old 0.3518 sat ~11:30, triggering clean early).
    // Pots are non-linear (10k pull-down, daisy-uzd). Re-measure if pull-down changes.
    constexpr float LOFI_CENTER   = 0.458f;  // fleet mean 12 o'clock (modules 1/2/4/5: 448/473/445/466,
                                             // mean 458; recal 2026-07-28 -- all noons now inside the deadzone)
    constexpr float POT_MAX       = 0.9597f; // measured POT_LEVEL full CW (recal 2026-07-23;
                                             // old 0.8813 was hit ~4:15, wasting the top of the throw)
    constexpr float POT_MIN       = 0.0f;    // measured POT_LEVEL full CCW (~0.0002)
    constexpr float LOFI_DEADZONE = 0.02f;   // symmetric clean band around center

    // CW staging (fractions of the CW throw, 0 = center .. 1 = full CW):
    //   sample-rate reduction (exp, full range) + saturation (linear) + overdrive
    constexpr float SRR_MAX       = 154;     // max sample-and-hold length (exp ramp; 60% of prior 256)
    constexpr float CW_SAT_DRIVE  = 3.0f;    // CW soft-saturation drive at full CW
    constexpr float DIST_DRIVE    = 6.0f;    // CW overdrive/distortion gain at full CW

    // CCW staging (fractions of the CCW throw):
    //   LP (early) -> drive + wavefold + saturation (late)
    constexpr float FILTER_END    = 0.6f;    // LP fully closed by ~0.6 of the CCW throw
    constexpr float FOLD_START    = 0.40f;   // drive+wavefold begins (~10 o'clock)
    constexpr float LP_COEFF_MIN  = 0.025f;  // 2-pole LP floor (darker, steeper)
    constexpr float DRIVE_MAX     = 24.0f;   // max pre-fold drive
    constexpr float SAT_DRIVE     = 3.0f;    // CCW saturation (soft-clip) drive at full fold

    // Phaser runs poles_ parallel allpass engines and sums them WITHOUT applying
    // its gain_frac_, so normalize by 1/poles to land back at an equal dry/wet mix.
    constexpr int   PHASER_POLES = 4;
    constexpr float PHASER_SCALE = 1.0f / (float)PHASER_POLES;

    // FX flavour-param ranges. Each 0..99 config value maps linearly onto these.
    constexpr float LFO_RATE_MIN  = 0.05f;   // Hz (all three LFOs share this scale)
    constexpr float LFO_RATE_MAX  = 4.0f;    // Hz
    constexpr float CHORUS_DEPTH_MAX = 0.90f;
    constexpr float FLANGER_FB_MAX   = 0.92f; // keep below 1 to avoid runaway
    constexpr float PHASER_FREQ_MIN  = 100.0f;
    constexpr float PHASER_FREQ_MAX  = 2000.0f;
    // Per-voice LFO-rate offset so the two outputs aren't identical (stereo move).
    constexpr float VOICE2_RATE_MULT = 1.20f;

    // --- Variant params ---
    // Chorus Ensemble: 3 detuned voices. Wider rate spread + distinct delay
    // times so the voices separate (lusher, more obvious beating).
    constexpr float ENS_RATIO[3]  = {0.70f, 1.0f, 1.45f};
    constexpr float ENS_DELAY[3]  = {0.35f, 0.62f, 0.92f};  // SetDelay 0..1 -> ~2.9/5.0/7.4 ms
    constexpr float ENS_RATE_MAX  = 1.8f;     // base LFO Hz at full param a
    // Flanger Barber-pole: sweep speed + comb geometry (samples @ 48kHz).
    constexpr float BP_MAX_SPEED_HZ = 0.5f;   // sweep cycles/sec at the extremes
    constexpr float BP_MIN_DELAY    = 48.0f;  // ~1.0 ms min flange delay
    constexpr float BP_RANGE        = 360.0f; // ~7.5 ms sweep range
    // Phaser Bi-phase: centre freq + 2nd-LFO depth; ratio range for param b.
    constexpr float BI_CENTRE_HZ  = 600.0f;
    constexpr float BI_DEPTH_HZ   = 450.0f;
    constexpr int   BI_POLES      = 8;

    // --- Internal LPG (daisy-nmr) ---
    // Decay is exponential in TIME, in TWO segments with the knee at field 50
    // (daisy-ygh). A single exponential 2ms..10s put a sharp pluck at 50 and
    // spent the whole upper half on times nobody reached for; the knee moves the
    // old top (10s) down to 50 and gives the entire upper half to long decays.
    // Short plucks deliberately get less parameter space as a result.
    //   field  0..50 : 2 ms .. 10 s   (~18.4% per step -- coarser than before)
    //   field 50..99 : 10 s .. 20 s   (~1.4% per step -- very fine up top)
    // The ratio-per-step changes at the knee; that kink is the point, not a bug.
    constexpr float LPG_DECAY_MIN_S = 0.002f;   // field 0  = 2 ms (percussive tick)
    constexpr float LPG_DECAY_MID_S = 10.0f;    // field 50 = 10 s (the old maximum)
    constexpr float LPG_DECAY_MAX_S = 20.0f;    // field 99 = 20 s (a long swell)
    constexpr int   LPG_DECAY_KNEE  = 50;       // field where MID_S lands
    constexpr int   LPG_DECAY_TOP   = 99;       // top of the field range
    // Decay SHAPE. A plain one-pole (asymptotic to zero) drops hard in the first
    // instant and then trails off quietly for ages -- the note loses presence
    // almost immediately. Instead the one-pole aims at a NEGATIVE target -U and
    // we take only the part above zero: the further below zero it aims, the
    // straighter the visible portion. U -> inf is a linear ramp, U -> 0 is the
    // old pure exponential.
    //   U = 0.10 -> the audible span is ln(1.1/0.1) = 2.4 time constants, about
    //   half the curvature of the old -40 dB exponential (4.6 tau). At the
    //   halfway point the envelope now sits ~7 dB higher: the note holds, then
    //   falls away, rather than vanishing and leaving a tail.
    // Bonus: the envelope reaches TRUE zero at the set time, so the decay figure
    // is now the actual length of the note, not a -40 dB approximation.
    constexpr float LPG_DECAY_UNDERSHOOT = 0.10f;
    // Attack: sharp but never a click. Scaled to the decay so a 2 ms pluck isn't
    // blunted by a 1 ms ramp, and clamped to the ~1 ms vactrol rise at the top.
    constexpr float LPG_ATTACK_FRAC  = 0.15f;   // of the decay time
    constexpr float LPG_ATTACK_MIN_S = 0.00015f;
    constexpr float LPG_ATTACK_MAX_S = 0.001f;
    // Cutoff sweep: closed .. open, exponential (equal octaves per envelope step).
    // LPG_FC_TRACK warps how the cutoff follows the envelope. >1 makes the filter
    // run AHEAD of the VCA -- it darkens faster than the note gets quieter, which
    // is what reads as an aggressive gate. At 1.5, half-envelope sits at ~280 Hz
    // instead of ~690 Hz. (Raise toward 2 for more; the next lever after that is
    // more poles, currently 2.)
    constexpr float LPG_FC_MIN_HZ = 20.0f;
    constexpr float LPG_FC_MAX_HZ = 16000.0f;
    constexpr float LPG_FC_TRACK  = 1.5f;
    constexpr float LPG_COEFF_MAX = 0.95f;      // one-pole ceiling (stability)
    // Below this the gate is shut: zero the envelope and flush the filters so a
    // 10 s tail doesn't trickle denormals through four one-poles forever.
    constexpr float LPG_ENV_FLOOR = 1.0e-5f;

    // Field value (0..99) -> decay seconds, across the two-segment curve above.
    inline float LpgDecaySeconds(uint8_t v) {
        if ((int)v <= LPG_DECAY_KNEE) {
            float x = (float)v / (float)LPG_DECAY_KNEE;
            return LPG_DECAY_MIN_S * powf(LPG_DECAY_MID_S / LPG_DECAY_MIN_S, x);
        }
        float x = (float)((int)v - LPG_DECAY_KNEE)
                / (float)(LPG_DECAY_TOP - LPG_DECAY_KNEE);
        return LPG_DECAY_MID_S * powf(LPG_DECAY_MAX_S / LPG_DECAY_MID_S, x);
    }

    inline float Norm99(uint8_t v) { return (float)v / 99.0f; }
    inline float MapRange(uint8_t v, float lo, float hi) { return lo + Norm99(v) * (hi - lo); }
    inline float MixStage(float dry, float wet, float m) { return dry + m * (wet - dry); }
}

void AudioPipeline::Init() {
    srrFactor_ = 1;
    hpCoeff_   = 0.0f;
    satAmt_    = 0.0f;
    lpCoeff_   = 1.0f;
    driveGain_ = 1.0f;
    foldMix_   = 0.0f;
    lofiClean_ = true;
    lofi1_ = LofiState();
    lofi2_ = LofiState();

    // FX engines (per voice). Depth/feedback that the menu doesn't expose keep a
    // fixed character; rate/depth/feedback/freq are applied by SetFxChain below.
    for (int i = 0; i < 3; i++) {
        chorus1_[i].Init(48000.0f); chorus1_[i].SetFeedback(0.2f);
        chorus2_[i].Init(48000.0f); chorus2_[i].SetFeedback(0.2f);
    }
    flanger1_.Init(48000.0f); flanger2_.Init(48000.0f);
    flanger1_.SetLfoDepth(0.85f); flanger2_.SetLfoDepth(0.85f);
    phaser1_.Init(48000.0f);  phaser2_.Init(48000.0f);
    phaser1_.SetPoles(PHASER_POLES); phaser2_.SetPoles(PHASER_POLES);
    phaser1_.SetLfoDepth(0.9f); phaser2_.SetLfoDepth(0.9f);
    bpFl1_.Init(); bpFl2_.Init();

    // LPG cutoff LUT: env 0..1 -> one-pole coefficient, exponential in cutoff.
    for (int i = 0; i < LPG_LUT_N; i++) {
        float e  = (float)i / (float)(LPG_LUT_N - 1);
        float g  = powf(e, LPG_FC_TRACK);   // filter leads the VCA (see LPG_FC_TRACK)
        float fc = LPG_FC_MIN_HZ * powf(LPG_FC_MAX_HZ / LPG_FC_MIN_HZ, g);
        float c  = 1.0f - expf(-2.0f * 3.14159265f * fc / 48000.0f);
        if (c > LPG_COEFF_MAX) c = LPG_COEFF_MAX;
        lpgCoefLut_[i] = c;
    }
    lpgEnv_ = 0.0f;
    lpgAttacking_ = false;
    lpgTrigPending_ = false;

    SetFxChain(DefaultFxChain());
}

FxChainConfig AudioPipeline::DefaultFxChain() {
    // Boots tasteful: a gentle chorus on, flanger/phaser available but at 0 level.
    FxChainConfig c;
    c.enabled = 1;       // FX chain on by default
    c.chorusLevel  = 45; c.chorusType  = 0; c.chorusP1  = 12; c.chorusP2  = 80;
    c.flangerLevel = 0;  c.flangerType = 0; c.flangerP1 = 12; c.flangerP2 = 50;
    c.phaserLevel  = 0;  c.phaserType  = 0; c.phaserP1  = 12; c.phaserP2  = 25;
    c.parallel     = 0;  // serial
    c.paramQuant   = 0;  // A/B interpolation off
    c.out2Drone    = 0;  // Out2 coupled (normal) by default
    c.cvOutMode    = 0;  // CV out = envelope follower of Out1 by default
    c.timbreCvRoute = 0; // CV A/B -> Param A/B (normal) by default
    c.lpgEnabled   = 0;  // LPG off (the module drones freely until you ask for a gate)
    c.lpgDecay     = 20; // ~60 ms: a short, percussive pluck (was 40 pre-daisy-ygh)
    return c;
}

void AudioPipeline::SetFxChain(const FxChainConfig& c) {
    fxEnabled_   = (c.enabled != 0);
    chorusMixF_  = Norm99(c.chorusLevel);
    flangerMixF_ = Norm99(c.flangerLevel);
    phaserMixF_  = Norm99(c.phaserLevel);
    fxParallel_  = (c.parallel != 0);
    chorusType_  = c.chorusType;
    flangerType_ = c.flangerType;
    phaserType_  = c.phaserType;

    // ---- Internal LPG (daisy-nmr) ----
    {
        float decay = LpgDecaySeconds(c.lpgDecay);
        // Solve the undershoot one-pole for "reaches exactly 0 after `decay`":
        //   -U + (1+U)*coef^n = 0  ->  coef = (U/(1+U))^(1/n)
        const float U = LPG_DECAY_UNDERSHOOT;
        lpgDecayCoef_ = expf(logf(U / (1.0f + U)) / (decay * 48000.0f));
        float attack = decay * LPG_ATTACK_FRAC;
        if (attack < LPG_ATTACK_MIN_S) attack = LPG_ATTACK_MIN_S;
        if (attack > LPG_ATTACK_MAX_S) attack = LPG_ATTACK_MAX_S;
        lpgAttackInc_ = 1.0f / (attack * 48000.0f);

        const bool on = (c.lpgEnabled != 0);
        // Switching the LPG ON plucks it once: without a trigger the gate sits
        // shut, so the menu edit would otherwise just drop the module to silence
        // with no clue why. One pluck confirms the setting AND the decay you've
        // dialled in. Switching OFF reopens the gate (env is then unused).
        if (on && !lpgEnabled_) lpgTrigPending_ = true;
        lpgEnabled_ = on;
    }

    // ---- Chorus: type 0 clean (1 voice), type 1 Ensemble (3 detuned voices) ----
    if (chorusType_ == 1) {
        float base  = MapRange(c.chorusP1, 0.10f, ENS_RATE_MAX);   // slower, lush
        float depth = 0.55f + Norm99(c.chorusP2) * 0.38f;          // 0.55..0.93 (deep)
        for (int i = 0; i < 3; i++) {
            chorus1_[i].SetLfoFreq(base * ENS_RATIO[i]);
            chorus2_[i].SetLfoFreq(base * ENS_RATIO[i] * VOICE2_RATE_MULT);
            chorus1_[i].SetLfoDepth(depth); chorus2_[i].SetLfoDepth(depth);
            chorus1_[i].SetDelay(ENS_DELAY[i]); chorus2_[i].SetDelay(ENS_DELAY[i]);
        }
    } else {
        float cr = MapRange(c.chorusP1, LFO_RATE_MIN, LFO_RATE_MAX);
        float cd = Norm99(c.chorusP2) * CHORUS_DEPTH_MAX;
        chorus1_[0].SetLfoFreq(cr); chorus2_[0].SetLfoFreq(cr * VOICE2_RATE_MULT);
        chorus1_[0].SetLfoDepth(cd); chorus2_[0].SetLfoDepth(cd);
    }

    // ---- Flanger: type 0 clean, type 1 Barber-pole (infinite sweep) ----
    if (flangerType_ == 1) {
        // param a: 0..49 = downward sweep, 50..99 = upward (50 = stopped).
        float dir = ((int)c.flangerP1 - 50) / 50.0f;     // -1..+0.98
        bpInc_      = (dir * BP_MAX_SPEED_HZ) / 48000.0f;
        bpFeedback_ = Norm99(c.flangerP2) * 0.90f;
    } else {
        float fr = MapRange(c.flangerP1, LFO_RATE_MIN, LFO_RATE_MAX);
        float ff = Norm99(c.flangerP2) * FLANGER_FB_MAX;
        flanger1_.SetLfoFreq(fr); flanger2_.SetLfoFreq(fr * VOICE2_RATE_MULT);
        flanger1_.SetFeedback(ff); flanger2_.SetFeedback(ff);
    }

    // ---- Phaser: type 0 clean (4-pole), type 1 Bi-phase (8-pole + 2nd LFO) ----
    if (phaserType_ == 1) {
        float r1 = MapRange(c.phaserP1, LFO_RATE_MIN, LFO_RATE_MAX);
        phaser1_.SetPoles(BI_POLES);  phaser2_.SetPoles(BI_POLES);
        phaser1_.SetLfoFreq(r1);      phaser2_.SetLfoFreq(r1 * VOICE2_RATE_MULT);
        float ratio = 0.20f + Norm99(c.phaserP2) * 2.80f;   // 2nd-LFO rate ratio 0.2..3
        biLfoInc_   = (r1 * ratio) / 48000.0f;
        biCentre_   = BI_CENTRE_HZ;
        biDepth_    = BI_DEPTH_HZ;
        phaserScale_ = 1.0f / (float)BI_POLES;
    } else {
        float pr = MapRange(c.phaserP1, LFO_RATE_MIN, LFO_RATE_MAX);
        float pf = MapRange(c.phaserP2, PHASER_FREQ_MIN, PHASER_FREQ_MAX);
        phaser1_.SetPoles(PHASER_POLES); phaser2_.SetPoles(PHASER_POLES);
        phaser1_.SetLfoFreq(pr);         phaser2_.SetLfoFreq(pr * VOICE2_RATE_MULT);
        phaser1_.SetFreq(pf);            phaser2_.SetFreq(pf);
        phaserScale_ = PHASER_SCALE;
    }
}

float AudioPipeline::Wavefold(float x) {
    // Triangle wavefolder (period 4): folds any input magnitude back into
    // [-1, 1], adding harmonics. Passes through unchanged for |x| <= 1.
    float q = fmodf(x + 1.0f, 4.0f);
    if (q < 0.0f) q += 4.0f;
    float tri = (q <= 2.0f) ? q : 4.0f - q;
    return tri - 1.0f;
}

void AudioPipeline::SetLofiMacro(float pot) {
    if (pot > LOFI_CENTER + LOFI_DEADZONE) {
        // CW: sample-rate reduction (exp, over the whole half) + linear soft
        // saturation + an overdrive/distortion stage for grit. No high-pass.
        lofiClean_ = false;
        float cw = (pot - (LOFI_CENTER + LOFI_DEADZONE)) / (POT_MAX - (LOFI_CENTER + LOFI_DEADZONE));
        if (cw > 1.0f) cw = 1.0f;

        // SRR: exponential (geometric) ramp over the FULL CW half: 1 -> SRR_MAX
        // (gentle onset just past center, accelerating toward full CW).
        srrFactor_ = (int)(powf((float)SRR_MAX, cw) + 0.5f);
        if (srrFactor_ < 1) srrFactor_ = 1;

        // Soft saturation and the overdrive/distortion both ramp linearly with cw.
        satAmt_  = cw;
        distAmt_ = cw;

        // Resonant band-pass sweep: cutoff rises and Q rises (bandwidth narrows)
        // over the throw; wet mix from 0 (no filtering at center) to bpMixMax.
        // sweepCurve (<1) front-loads cutoff + mix so more happens early in the throw.
        float curve = g_lofiConfig.sweepCurve;
        if (curve < 0.1f) curve = 0.1f;
        float shaped = powf(cw, curve);
        float fc = g_lofiConfig.bpCutoffStartHz
                 + shaped * (g_lofiConfig.bpCutoffEndHz - g_lofiConfig.bpCutoffStartHz);
        float q  = g_lofiConfig.bpQStart
                 + cw * (g_lofiConfig.bpQEnd - g_lofiConfig.bpQStart);
        float f = 2.0f * sinf(3.14159265f * fc / 48000.0f);
        if (f > 0.99f) f = 0.99f;
        if (f < 0.0f)  f = 0.0f;
        bpF_ = f;
        float damp = (q > 0.05f) ? 1.0f / q : 1.0f / 0.05f;
        if (damp > 2.0f)  damp = 2.0f;
        if (damp < 0.02f) damp = 0.02f;
        bpDamp_ = damp;
        bpMix_  = shaped * g_lofiConfig.bpMixMax;

        hpCoeff_ = 0.0f; lpCoeff_ = 1.0f; driveGain_ = 1.0f; foldMix_ = 0.0f;
    } else if (pot < LOFI_CENTER - LOFI_DEADZONE) {
        // CCW: stage 1 low-pass (early), stage 2 drive + wavefold + saturation (late).
        lofiClean_ = false;
        float ccw = ((LOFI_CENTER - LOFI_DEADZONE) - pot) / ((LOFI_CENTER - LOFI_DEADZONE) - POT_MIN);
        if (ccw > 1.0f) ccw = 1.0f;

        // Stage 1: LP sweeps from open (1.0) to closed (LP_COEFF_MIN) over [0, FILTER_END].
        // Exponential (log-frequency) sweep -> cutoff drops by equal octaves per degree.
        float fAmt = ccw / FILTER_END;
        if (fAmt > 1.0f) fAmt = 1.0f;
        lpCoeff_ = powf(LP_COEFF_MIN, fAmt);

        // Stage 2: drive + wavefold blends in over [FOLD_START, 1]
        float dAmt = (ccw - FOLD_START) / (1.0f - FOLD_START);
        if (dAmt < 0.0f) dAmt = 0.0f;
        if (dAmt > 1.0f) dAmt = 1.0f;
        driveGain_ = 1.0f + dAmt * (DRIVE_MAX - 1.0f);
        foldMix_   = dAmt;

        srrFactor_ = 1; hpCoeff_ = 0.0f; satAmt_ = 0.0f; distAmt_ = 0.0f; bpMix_ = 0.0f;
    } else {
        // Center deadzone: clean
        lofiClean_ = true;
        srrFactor_ = 1; hpCoeff_ = 0.0f; satAmt_ = 0.0f; distAmt_ = 0.0f; bpMix_ = 0.0f;
        lpCoeff_ = 1.0f; driveGain_ = 1.0f; foldMix_ = 0.0f;
    }
}

float AudioPipeline::ProcessLofi(float v, LofiState& s) {
    // Sample-rate reduction (sample & hold) -- HP/SRR (CW) side
    if (srrFactor_ > 1) {
        if (s.srrCounter <= 0) {
            s.srrHeld = v;
            s.srrCounter = srrFactor_;
        }
        s.srrCounter--;
        v = s.srrHeld;
    }
    // One-pole high-pass on the stepped signal -> tinny/clicky (bypass at 0)
    if (hpCoeff_ > 0.0f) {
        s.hpLpState += hpCoeff_ * (v - s.hpLpState);
        v = v - s.hpLpState;
    }
    // Soft saturation that rides with the SRR (warmth on the stepped signal)
    if (satAmt_ > 0.0f) {
        float sat = tanhf(v * (1.0f + satAmt_ * (CW_SAT_DRIVE - 1.0f)));
        v += satAmt_ * (sat - v);
    }
    // Overdrive / distortion: gain into a hard clip -> gritty (bypass at 0) -- CW side
    if (distAmt_ > 0.0f) {
        float drive = 1.0f + distAmt_ * (DIST_DRIVE - 1.0f);
        float d = v * drive;
        if (d >  1.0f) d =  1.0f;
        if (d < -1.0f) d = -1.0f;
        v += distAmt_ * (d - v);
    }
    // 2-pole low-pass (lpCoeff_=1 => open, v unchanged) -- LP/fold (CCW) side
    s.lpState  += lpCoeff_ * (v - s.lpState);
    s.lpState2 += lpCoeff_ * (s.lpState - s.lpState2);
    v = s.lpState2;
    // drive -> wavefold -> saturation, blended in (foldMix_=0 => bypass)
    if (foldMix_ > 0.0f) {
        float folded = Wavefold(v * driveGain_);
        float processed = tanhf(folded * (1.0f + foldMix_ * (SAT_DRIVE - 1.0f)));
        v += foldMix_ * (processed - v);
    }
    // Resonant band-pass sweep (CW; SVF, bpMix_=0 => bypass). Blended dry->BP.
    if (bpMix_ > 0.0f) {
        s.svfLp += bpF_ * s.svfBp;
        float hp = v - s.svfLp - bpDamp_ * s.svfBp;
        s.svfBp += bpF_ * hp;
        // safety clamp (resonance can ring at high Q)
        if (s.svfBp >  4.0f) s.svfBp =  4.0f; else if (s.svfBp < -4.0f) s.svfBp = -4.0f;
        if (s.svfLp >  4.0f) s.svfLp =  4.0f; else if (s.svfLp < -4.0f) s.svfLp = -4.0f;
        v += bpMix_ * (s.svfBp - v);
    }
    return v;
}

// Post-Lo-Fi FX chain. Out1/Out2 are a stereo pair, each with its own engines.
// Each engine's Process() returns an equal wet/dry mix (the effect's normal
// sound); the per-stage mix crossfades dry -> that sound (mix 0 = bypass).
// All three engines run every sample (state stays warm) regardless of mix.
//
//   serial   : stages feed each other (chorus -> flanger -> phaser)
//   parallel : each stage processes the dry signal; their (wet-dry) deltas are
//              summed onto the dry (a send/return mix)
// --- Chorus stage: clean (1 voice) or Ensemble (3 detuned voices summed) ---
float AudioPipeline::ProcessChorus(float in, daisysp::ChorusEngine eng[3]) {
    if (chorusType_ == 1) {
        // Recover each engine's pure wet (engine returns (in+wet)/2) and sum the
        // three detuned voices for a lush, beating ensemble.
        float w = (2.0f * eng[0].Process(in) - in)
                + (2.0f * eng[1].Process(in) - in)
                + (2.0f * eng[2].Process(in) - in);
        // Wet-forward blend (less dry, stronger detuned voices) for a lush ensemble.
        return 0.25f * in + 0.38f * w;
    }
    return eng[0].Process(in);
}

// --- Flanger stage: clean or Barber-pole (two windowed taps, endless sweep) ---
float AudioPipeline::ProcessFlanger(float in, daisysp::Flanger& clean,
                                    daisysp::DelayLine<float, 2048>& bp, float bpPhase) {
    if (flangerType_ == 1) {
        float p1 = bpPhase;
        float p2 = bpPhase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        float r1 = bp.Read(BP_MIN_DELAY + p1 * BP_RANGE);
        float r2 = bp.Read(BP_MIN_DELAY + p2 * BP_RANGE);
        // Raised-sine window: each tap fades to 0 at its phase ends, so when one
        // wraps the other carries the sweep -> continuous (Shepard) flange.
        float wet = sinf(3.14159265f * p1) * r1 + sinf(3.14159265f * p2) * r2;
        bp.Write(in + bpFeedback_ * wet);
        return in + wet;
    }
    return clean.Process(in);
}

// --- Phaser stage: clean (4-pole) or Bi-phase (8-pole + 2nd LFO on centre) ---
float AudioPipeline::ProcessPhaser(float in, daisysp::Phaser& ph, float biLfo) {
    if (phaserType_ == 1) {
        ph.SetFreq(biCentre_ + biLfo * biDepth_);
    }
    return ph.Process(in) * phaserScale_;
}

void AudioPipeline::ProcessFx(float& l, float& r) {
    if (!fxEnabled_) return;   // global FX off -> dry pass-through

    // Advance the shared variant phasors once per sample.
    if (flangerType_ == 1) {
        bpPhase_ += bpInc_;
        if (bpPhase_ >= 1.0f) bpPhase_ -= 1.0f;
        else if (bpPhase_ < 0.0f) bpPhase_ += 1.0f;
    }
    float biLfo = 0.0f;
    if (phaserType_ == 1) {
        biLfoPhase_ += biLfoInc_;
        if (biLfoPhase_ >= 1.0f) biLfoPhase_ -= 1.0f;
        biLfo = sinf(2.0f * 3.14159265f * biLfoPhase_);
    }
    float p2 = bpPhase_ + 0.25f; if (p2 >= 1.0f) p2 -= 1.0f;  // voice-2 sweep offset

    if (fxParallel_) {
        float dl = l, dr = r;
        float al = dl, ar = dr;
        al += chorusMixF_  * (ProcessChorus(dl, chorus1_)                 - dl);
        ar += chorusMixF_  * (ProcessChorus(dr, chorus2_)                 - dr);
        al += flangerMixF_ * (ProcessFlanger(dl, flanger1_, bpFl1_, bpPhase_) - dl);
        ar += flangerMixF_ * (ProcessFlanger(dr, flanger2_, bpFl2_, p2)       - dr);
        al += phaserMixF_  * (ProcessPhaser(dl, phaser1_, biLfo)          - dl);
        ar += phaserMixF_  * (ProcessPhaser(dr, phaser2_, biLfo)          - dr);
        l = al; r = ar;
    } else {
        l = MixStage(l, ProcessChorus(l, chorus1_),                 chorusMixF_);
        r = MixStage(r, ProcessChorus(r, chorus2_),                 chorusMixF_);
        l = MixStage(l, ProcessFlanger(l, flanger1_, bpFl1_, bpPhase_), flangerMixF_);
        r = MixStage(r, ProcessFlanger(r, flanger2_, bpFl2_, p2),       flangerMixF_);
        l = MixStage(l, ProcessPhaser(l, phaser1_, biLfo),          phaserMixF_);
        r = MixStage(r, ProcessPhaser(r, phaser2_, biLfo),          phaserMixF_);
    }
}

// --- Internal LPG (daisy-nmr) -----------------------------------------------
// Advance the shared envelope one sample. A pending trigger (from the Sync EXTI
// ISR, or the menu switching the LPG on) restarts the attack from wherever the
// envelope currently is -- retriggering mid-decay swells rather than clicking.
void AudioPipeline::LpgUpdateEnvelope() {
    if (lpgTrigPending_) {
        lpgTrigPending_ = false;
        lpgAttacking_   = true;
    }
    if (lpgAttacking_) {
        lpgEnv_ += lpgAttackInc_;      // linear ramp: sharp and exactly timed
        if (lpgEnv_ >= 1.0f) { lpgEnv_ = 1.0f; lpgAttacking_ = false; }
        return;
    }
    // One-pole aimed BELOW zero (see LPG_DECAY_UNDERSHOOT) -- straighter fall,
    // and it genuinely arrives at zero instead of trailing off asymptotically.
    lpgEnv_ = (lpgEnv_ + LPG_DECAY_UNDERSHOOT) * lpgDecayCoef_ - LPG_DECAY_UNDERSHOOT;
    if (lpgEnv_ < LPG_ENV_FLOOR) lpgEnv_ = 0.0f;   // shut (and stays shut: the
                                                   // update keeps going negative)
}

// Envelope -> low-pass coefficient, from the exponential-cutoff LUT.
float AudioPipeline::LpgCoeff(float env) const {
    if (env <= 0.0f) return lpgCoefLut_[0];
    if (env >= 1.0f) return lpgCoefLut_[LPG_LUT_N - 1];
    float x = env * (float)(LPG_LUT_N - 1);
    int   i = (int)x;
    if (i > LPG_LUT_N - 2) i = LPG_LUT_N - 2;
    float f = x - (float)i;
    return lpgCoefLut_[i] + f * (lpgCoefLut_[i + 1] - lpgCoefLut_[i]);
}

// One voice through the gate: 2-pole low-pass (cutoff tracks the envelope) into
// the VCA (gain IS the envelope). Both from the same envelope -- that coupling
// is what makes it an LPG rather than a filter and a VCA that happen to share
// a trigger.
float AudioPipeline::ProcessLpg(float v, LofiState& s, float coeff) {
    if (lpgEnv_ <= 0.0f) {         // gate shut: flush state so the next pluck is clean
        s.lpgLp1 = 0.0f;
        s.lpgLp2 = 0.0f;
        return 0.0f;
    }
    s.lpgLp1 += coeff * (v - s.lpgLp1);
    s.lpgLp2 += coeff * (s.lpgLp1 - s.lpgLp2);
    return s.lpgLp2 * lpgEnv_;
}

void AudioPipeline::Process(BytebeatEngine& engine, float** out, size_t size) {
    // Out2 decouple / drone (daisy-pcq): the raw frozen voice bypasses Lo-Fi + FX.
    // We still run its Lo-Fi + FX engines on the live signal so their state stays
    // warm and re-coupling is click-free -- we just don't route them to the output.
    const bool out2Drone = engine.GetOut2Decoupled();
    for (size_t i = 0; i < size; i++) {
        float o1, o2;
        engine.Process(o1, o2);
        cleanBuffer_[i]  = o1;  // Out1 pre-lo-fi -> CV/BPM analysis
        cleanBuffer2_[i] = o2;  // Out2 pre-lo-fi -> CV DC-out mode (daisy-0pq)
        float v1 = ProcessLofi(o1, lofi1_);
        float v2 = ProcessLofi(o2, lofi2_);
        ProcessFx(v1, v2);     // post-Lo-Fi FX insert (stereo pair)
        // Internal LPG (daisy-nmr): last in the chain, so it gates the finished
        // sound. The envelope advances even while bypassed -- a trigger arriving
        // just before you switch the LPG on then behaves the same either way.
        LpgUpdateEnvelope();
        if (lpgEnabled_) {
            const float coeff = LpgCoeff(lpgEnv_);
            v1 = ProcessLpg(v1, lofi1_, coeff);
            v2 = ProcessLpg(v2, lofi2_, coeff);
        }
        out[0][i] = v1;
        // The drone deliberately ignores Sync (and Tone + FX), so the LPG -- a
        // Sync-triggered gate -- must not touch it either: it stays a free-run
        // drone under a plucked Out1.
        out[1][i] = out2Drone ? o2 : v2;
    }
}
