#pragma once
#include <cstdint>
#include "formulas.h"

// Dual-voice bytebeat engine. One master phase accumulator (clocked by voice 1 /
// Out1, the primary) produces two independent voices:
//   Out1 = formula1(t)        (primary; CV/gate are derived from this)
//   Out2 = formula2(t - delay)
// Bytebeat formulas are pure functions of t, so the Out2 "delay" is just an
// argument offset -- no delay buffer is needed.
class BytebeatEngine {
public:
    void Init();

    // Advance the master phase by one output sample (48kHz) and produce both
    // voices, each linearly interpolated and converted to [-1, 1].
    void Process(float& out1, float& out2);

    bool TChanged() const { return tChanged_; }
    uint8_t GetRawSample() const { return curA_; }  // Out1 raw (0-255)
    uint32_t GetT() const { return t_; }

    // Formula selection (voice 1 = Out1 primary, voice 2 = Out2)
    void SetFormula1(int index);
    void SetFormula2(int index);
    int GetFormula1Index() const { return formula1Index_; }
    int GetFormula2Index() const { return formula2Index_; }
    const FormulaInfo* GetFormula1() const {
        return const_cast<const FormulaInfo*>(formula1_);
    }

    // Out2 delay relative to Out1, in whole bytebeat cycles (8kHz steps).
    void SetDelay(int cycles);
    int GetDelay() const { return delay_; }

    // Shared parameters A and B (both voices use these)
    void SetParamA(int32_t a) { paramA_ = a; }
    void SetParamB(int32_t b) { paramB_ = b; }
    int32_t GetParamA() const { return paramA_; }
    int32_t GetParamB() const { return paramB_; }

    // Param interpolation grid step (daisy-gac): 0/1 = off (exact eval), else
    // quantise A,B to a grid of this step and bilinearly crossfade the formula
    // outputs at the surrounding grid corners. Tames A/B discontinuity.
    void SetParamQuant(int q) { paramQuant_ = q; }
    int GetParamQuant() const { return paramQuant_; }

    // Rate multiplier (0.25x to 4x; also scaled by external clock)
    void SetRate(float rate);
    float GetRate() const { return rateMultiplier_; }

    // V/oct pitch (daisy-c5i): internally hard-sync the master phase to freqHz so
    // the output is periodic at freqHz -> a clean tuned tone (the Rate knob then
    // sets timbre: how far t advances per cycle). 0 = disabled (Clock/normal mode).
    void SetPitchSync(float freqHz);

    // Out2 decouple / drone (daisy-pcq). When enabled, Out2 FORKS off the master:
    // on the coupled->decoupled edge it snapshots the current phase, rate and A/B,
    // then free-runs on its own accumulator at the frozen rate with the frozen A/B
    // -- unaffected by Clock/V-oct/Rate/gate or live A/B thereafter. Re-coupling
    // snaps Out2 back to the shared master phase + live params. Idempotent: only
    // the false->true transition snapshots, so this may be called every loop.
    void SetOut2Decoupled(bool decoupled);
    bool GetOut2Decoupled() const { return out2Decoupled_; }

    // Frozen drone state accessors (for persistence). Valid once decoupled; these
    // are the exact phase increment (rate) + A/B captured at the decouple edge.
    uint64_t GetDroneInc()    const { return phaseIncrement2_; }
    int32_t  GetDroneParamA() const { return paramA2_; }
    int32_t  GetDroneParamB() const { return paramB2_; }

    // Restore a decoupled drone from persisted state (used at power-on instead of
    // the live snapshot, so pitch + A/B come back bit-exact across a power cycle).
    // Call after SetFormula2(). Restarts the drone phase from t=0.
    void RestoreOut2Drone(uint64_t inc, int32_t a, int32_t b);

    // Reset master t to 0
    void Reset();

    // Request a sample-accurate hard-sync reset. Called from the gate EXTI ISR;
    // only sets a flag (no shared-state race) -- applied at the start of the
    // next audio sample in Process(), which restarts the waveform at t=0.
    void SyncReset() { syncPending_ = true; }

    // Freeze: stop advancing t
    void SetFreeze(bool freeze) { frozen_ = freeze; }
    bool GetFreeze() const { return frozen_; }

private:
    void UpdatePhaseIncrement();  // from voice 1's base sample rate

    // Master phase accumulator (32.32 fixed point)
    uint64_t phase_ = 0;
    uint64_t phaseIncrement_ = 0;
    uint32_t t_ = 0;
    bool tChanged_ = false;
    volatile bool syncPending_ = false;  // hard-sync reset requested by gate EXTI

    // V/oct hard-sync pitch: internal per-sample reset accumulator (0 inc = off).
    volatile float pitchSyncInc_ = 0.0f;  // freqHz / OUTPUT_SAMPLE_RATE
    float pitchSyncPhase_ = 0.0f;

    // Out2 decouple / drone (daisy-pcq): Out2's independent time base + frozen
    // params, all used only while out2Decoupled_. Seeded from the master on the
    // couple->decouple edge so there's no discontinuity at the switch.
    volatile bool out2Decoupled_ = false;
    uint64_t phase2_ = 0;
    uint64_t phaseIncrement2_ = 0;   // frozen master increment at decouple time
    uint32_t t2_ = 0;
    uint8_t  prevB2_ = 0, curB2_ = 0;
    int32_t  paramA2_ = 128, paramB2_ = 128;  // frozen A/B at decouple time

    // Voice 1 (Out1, primary) -- drives the master clock
    volatile int formula1Index_ = 0;
    volatile const FormulaInfo* formula1_ = nullptr;
    uint8_t prevA_ = 0, curA_ = 0;

    // Voice 2 (Out2)
    volatile int formula2Index_ = 0;
    volatile const FormulaInfo* formula2_ = nullptr;
    uint8_t prevB_ = 0, curB_ = 0;
    volatile int delay_ = 0;  // whole cycles

    // Shared parameters (volatile: written by main loop, read by audio ISR)
    volatile int32_t paramA_ = 1;
    volatile int32_t paramB_ = 1;
    volatile int paramQuant_ = 0;  // 0/1 = off; else A/B interpolation grid step

    float rateMultiplier_ = 1.0f;
    volatile bool frozen_ = false;
};
