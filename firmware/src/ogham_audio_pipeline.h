#pragma once
#include "bytebeat_engine.h"
#include "Effects/chorus.h"
#include "Effects/flanger.h"
#include "Effects/phaser.h"
#include "Utility/delayline.h"

// FX chain config (Func long-press -> FX editor). A fixed 3-stage chain
// (Chorus -> Flanger -> Phaser); each stage has a mix (0 = bypass) and two
// flavour params. `parallel` swaps the topology (stages in parallel vs series).
// All values are 0..99 (displayed as 2 digits; mapped to engine ranges in
// AudioPipeline::SetFxChain). Shared across both voices; each voice keeps its
// own engine state. The field index order here MUST match the menu in
// ogham_main.cpp and the labels in Display::ShowFxEdit.
struct FxChainConfig {
    uint8_t enabled;    // global FX on/off (1 = on, 0 = whole chain bypassed)
    // Per stage: level (mix), type (variant 0/1), param1, param2.
    uint8_t chorusLevel,  chorusType,  chorusP1,  chorusP2;
    uint8_t flangerLevel, flangerType, flangerP1, flangerP2;
    uint8_t phaserLevel,  phaserType,  phaserP1,  phaserP2;
    uint8_t parallel;   // 0 = serial, 1 = parallel
    uint8_t paramQuant; // A/B interpolation grid step (engine setting; 0 = off)
    uint8_t out2Drone;  // Out2 decouple/drone (daisy-pcq): 1 = frozen independent voice
    uint8_t cvOutMode;  // CV-out mode (daisy-0pq): 0 = env follower, 1 = DC Out1, 2 = DC Out2
    uint8_t timbreCvRoute; // CV->Timbre routing (daisy-gtw): 0 = normal, 1 = CV A, 2 = CV B
    uint8_t lpgEnabled; // internal LPG (daisy-nmr): 1 = on (Sync in plucks it), 0 = bypassed
    uint8_t lpgDecay;   // LPG decay 0..99, exp-mapped 2ms..10s (see LPG_DECAY_MIN/MAX_S)
};
// FX menu fields: [0] global on/off, [1..12] = 3 stages x 4 sub-params
// (level/type/p1/p2), [13] = chain toggle, [14] = param-interp grid (q),
// [15] = Out2 decouple/drone, [16] = CV-out mode, [17] = CV->Timbre routing,
// [18] = LPG on/off, [19] = LPG decay. Stage param = 1 + stage*4 + sub.
static constexpr int FX_NUM_FIELDS   = 20;
static constexpr int FX_FIELD_GLOBAL = 0;   // the global on/off
static constexpr int FX_FIELD_CHAIN  = 13;  // the serial/parallel toggle
static constexpr int FX_FIELD_QUANT  = 14;  // the A/B param-interp grid (q)
static constexpr int FX_FIELD_DRONE  = 15;  // Out2 decouple/drone toggle
static constexpr int FX_FIELD_CVOUT  = 16;  // CV-out mode (env / DC Out1 / DC Out2)
static constexpr int FX_FIELD_TIMBRECV = 17; // CV->Timbre routing (normal / CV A / CV B)
static constexpr int FX_FIELD_LPG      = 18; // internal LPG on/off
static constexpr int FX_FIELD_LPGDECAY = 19; // internal LPG decay (0..99)
static constexpr int FX_TYPE_MAX     = 1;   // 0 = clean, 1 = characterful variant

// Audio pipeline: takes the two engine voices and applies the same lo-fi tone
// macro (Pot 4) to each, then writes them to Out1 / Out2. The lo-fi coefficients
// are shared (one knob), but each output keeps its own filter state so the two
// independent voices are processed identically and without cross-talk.
//
// Signal flow per voice: engine -> lo-fi macro -> FX chain (post-Lo-Fi) ->
//                        LPG -> out.
// The FX stage treats Out1/Out2 as a stereo pair (per-voice engines).
class AudioPipeline {
public:
    void Init();

    // Process a block of audio (called from the audio ISR).
    // out: 2 output channels (Out1 = L, Out2 = R), size samples each.
    void Process(BytebeatEngine& engine, float** out, size_t size);

    // Output level (kept at unity; level handled by a downstream VCA/mixer)
    void SetLevel(float level) { level_ = level; }
    float GetLevel() const { return level_; }

    // Bipolar lo-fi macro from a 0..1 pot reading.
    //   center (12 o'clock) = clean; CCW = LPF -> drive/wavefold/sat;
    //   CW = HPF -> sample-rate reduction/sat.
    void SetLofiMacro(float pot);

    // True when the lo-fi macro is in its clean center deadzone (no processing).
    bool IsLofiClean() const { return lofiClean_; }

    // Apply an FX-chain config: stores the wet/dry mixes + topology and maps
    // the 0..99 flavour params onto both voices' engines. Call on any edit.
    void SetFxChain(const FxChainConfig& c);

    // The module's default FX chain (used before settings are loaded).
    static FxChainConfig DefaultFxChain();

    // --- Internal LPG (daisy-nmr) ---
    // Pluck the low-pass gate: sharp attack, then the configured exponential
    // decay. Called from the Sync-in EXTI ISR -- it only raises a flag, which
    // Process() consumes at the start of the next SAMPLE (not the next block),
    // so the pluck lands within one sample of the gate edge.
    void LpgTrigger() { lpgTrigPending_ = true; }

    // Pre-lo-fi clean voice buffers (feed CV / BPM analysis). Out1 -> BPM + CV;
    // Out2 -> CV DC-out mode (daisy-0pq). Both are raw formula output (pre Lo-Fi/FX).
    const float* GetCleanBuffer() const { return cleanBuffer_; }
    const float* GetCleanBuffer2() const { return cleanBuffer2_; }

private:
    static constexpr size_t MAX_BLOCK_SIZE = 256;
    float cleanBuffer_[MAX_BLOCK_SIZE] = {};
    float cleanBuffer2_[MAX_BLOCK_SIZE] = {};

    float Wavefold(float x);

    // Per-output lo-fi filter state (one set per voice; identical processing)
    struct LofiState {
        float lpState   = 0.0f;
        float lpState2  = 0.0f;
        float hpLpState = 0.0f;
        float srrHeld   = 0.0f;
        int   srrCounter = 0;
        float svfLp     = 0.0f;  // state-variable filter low-pass state
        float svfBp     = 0.0f;  // state-variable filter band-pass state
        float lpgLp1    = 0.0f;  // LPG 2-pole low-pass, stage 1
        float lpgLp2    = 0.0f;  // LPG 2-pole low-pass, stage 2
    };
    float ProcessLofi(float v, LofiState& s);

    // Post-Lo-Fi FX chain (stereo pair: l=Out1, r=Out2). Runs all three stages
    // (serial or parallel) using chorusN_/flangerN_/phaserN_ + the stored mixes.
    void ProcessFx(float& l, float& r);

    LofiState lofi1_;  // Out1
    LofiState lofi2_;  // Out2

    float level_ = 1.0f;

    // FX chain state. Mixes are 0..1 (from the 0..99 config); topology flag.
    float chorusMixF_  = 0.0f;
    float flangerMixF_ = 0.0f;
    float phaserMixF_  = 0.0f;
    bool  fxParallel_  = false;

    bool fxEnabled_ = true; // global FX on/off

    // Per-stage variant (0 = clean DaisySP, 1 = characterful variant).
    int chorusType_  = 0;   // 1 = Ensemble (Juno-style)
    int flangerType_ = 0;   // 1 = Barber-pole (infinite sweep)
    int phaserType_  = 0;   // 1 = Bi-phase (dual LFO)

    // Per-voice FX engines (one set each so Out1/Out2 stay independent).
    // Chorus uses up to 3 detuned voices for the Ensemble variant; clean uses [0].
    daisysp::ChorusEngine chorus1_[3];   // Out1
    daisysp::ChorusEngine chorus2_[3];   // Out2
    daisysp::Flanger      flanger1_;
    daisysp::Flanger      flanger2_;
    daisysp::Phaser       phaser1_;
    daisysp::Phaser       phaser2_;

    // Variant DSP helpers (branch on the per-stage type).
    float ProcessChorus (float in, daisysp::ChorusEngine eng[3]);
    float ProcessFlanger(float in, daisysp::Flanger& clean,
                         daisysp::DelayLine<float, 2048>& bp, float bpPhase);
    float ProcessPhaser (float in, daisysp::Phaser& ph, float biLfo);

    float phaserScale_ = 0.25f;  // 1/poles, normalizes the parallel-pole sum

    // --- Flanger Barber-pole variant state (one delay line + phasor per voice) ---
    daisysp::DelayLine<float, 2048> bpFl1_;
    daisysp::DelayLine<float, 2048> bpFl2_;
    float bpPhase_   = 0.0f;   // 0..1 sweep phasor (shared; voice2 offset by 0.5)
    float bpInc_     = 0.0f;   // per-sample phase increment (sign = direction)
    float bpFeedback_= 0.0f;   // 0..~0.9

    // --- Phaser Bi-phase variant state (2nd LFO modulating the centre freq) ---
    float biLfoPhase_ = 0.0f;  // 0..1 second-LFO phasor
    float biLfoInc_   = 0.0f;  // per-sample increment
    float biCentre_   = 600.0f;// base allpass centre freq (Hz)
    float biDepth_    = 0.0f;   // centre-freq sweep depth (Hz)

    // Shared lo-fi coefficients (set by SetLofiMacro from Pot 4)
    int   srrFactor_ = 1;     // sample-and-hold length; 1 = clean
    float hpCoeff_   = 0.0f;  // one-pole HP; 0 = bypass (unused since CW = SRR/dist)
    float satAmt_    = 0.0f;  // soft-saturation blend (CW)
    float distAmt_   = 0.0f;  // overdrive/distortion blend (CW); 0 = bypass
    float lpCoeff_   = 1.0f;  // 2-pole LP; 1 = open/no filtering
    float driveGain_ = 1.0f;  // pre-fold drive
    float foldMix_   = 0.0f;  // wavefold blend; 0 = bypass
    // Resonant band-pass sweep (CW side; SVF coeffs set from g_lofiConfig)
    float bpF_       = 0.0f;  // SVF frequency coeff = 2*sin(pi*fc/fs)
    float bpDamp_    = 1.0f;  // SVF damping = 1/Q
    float bpMix_     = 0.0f;  // wet/dry blend; 0 = bypass
    bool  lofiClean_ = true;  // true in the clean center deadzone

    // --- Internal LPG (daisy-nmr): vactrol-style low-pass gate ---------------
    // One shared envelope (single trigger source, single decay setting) drives
    // BOTH a VCA and a 2-pole low-pass per voice, so quiet is also dark -- the
    // vactrol behaviour that makes an LPG sound like a struck body rather than
    // a plain fade. Filter state is per-voice (LofiState.lpgLp1/2); the
    // envelope is shared so Out1/Out2 are plucked in lockstep.
    void  LpgUpdateEnvelope();          // advance the shared envelope one sample
    float ProcessLpg(float v, LofiState& s, float coeff);
    float LpgCoeff(float env) const;    // envelope -> LP coefficient (LUT + lerp)

    bool  lpgEnabled_ = false;
    volatile bool lpgTrigPending_ = false;  // set by the Sync EXTI ISR
    bool  lpgAttacking_ = false;   // true during the (short) attack ramp
    float lpgEnv_       = 0.0f;    // 0..1 shared envelope
    float lpgAttackInc_ = 1.0f;    // per-sample linear attack step
    float lpgDecayCoef_ = 0.0f;    // per-sample exponential decay multiplier

    // env -> one-pole LP coefficient, exponential in cutoff. 65 entries (env in
    // 1/64 steps) + linear interpolation keeps an expf/sinf out of the sample
    // loop; the mapping is smooth enough that 64 steps are inaudible.
    static constexpr int LPG_LUT_N = 65;
    float lpgCoefLut_[LPG_LUT_N] = {};
};

// Live-tunable Lo-Fi config (RAM, written by the monitor over SWD via mww).
// magic lets the monitor confirm it located the struct.
struct LofiConfig {
    uint32_t magic;            // 'LOFI' = 0x4C4F4649
    float bpCutoffStartHz;     // band-pass cutoff just past center
    float bpCutoffEndHz;       // band-pass cutoff at full CW
    float bpQStart;            // resonance/Q at center (low = wide)
    float bpQEnd;              // resonance/Q at full CW (high = narrow/resonant)
    float bpMixMax;            // max wet blend at full CW (0..1)
    float sweepCurve;          // shapes cutoff+mix vs throw: 1=linear, <1 front-loads
};
extern LofiConfig g_lofiConfig;
