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

#include "daisy_seed.h"
#include "util/PersistentStorage.h"
#include "ogham_pins.h"
#include "bytebeat_engine.h"
#include "ogham_audio_pipeline.h"
#include "ogham_controls.h"
#include "ogham_display.h"
#include "ogham_cv_output.h"
#include "bpm_clock.h"
#include "tm1637.h"
#include <cmath>
#include <cstring>

using namespace daisy;

// --- Global objects ---
static DaisySeed hw;
static Encoder encoder;
static GateIn gateIn;
static GateIn clockIn;
static GPIO gateOutGpio;

static BytebeatEngine engine;
static AudioPipeline pipeline;
static Controls controls;

// --- Encoder scan timer (daisy-szs) ---
// The quadrature decoder is POLLED (these pins have no timer-encoder alternate
// function, so hardware decoding isn't available, and EXTI on a mechanical
// encoder just floods the CPU with bounce). Polling only works if it outruns the
// contacts: a 24 PPR encoder is 96 quadrature transitions per revolution, so a
// hard 6-8 rev/s crank puts out 576-768 transitions/s. Sampling that from the
// 1kHz audio callback left barely one sample per transition -- transitions were
// missed, and a missed transition is a lost detent, exactly at the speeds the
// encoder ACCELERATION is designed for. 20kHz gives >26 samples per transition
// even at a full crank. Costs ~0.25% CPU (two GPIO reads + a table lookup).
// Falls back to the audio callback if the timer won't start.
//
// The rate was 20kHz by accident until 2026-08-10: the period was computed from
// GetPClk2Freq(), but TIM5 is an APB1 timer and an APB timer's kernel clock is
// twice PCLK (libDaisy's own GetTickFreq() returns GetPCLK1Freq()*2 for exactly
// that reason), so a period asking for 10kHz delivered 20kHz. Measured 20.1kHz
// on module 2. Now computed correctly and 20kHz chosen deliberately, since the
// headroom turned out to be worth having.
static TimerHandle encTim;
static bool encTimerRunning = false;
static constexpr uint32_t ENC_SCAN_HZ = 20000;
static void EncoderScanCallback(void*) { controls.SampleEncoder(); }
static TM1637 tm1637;
static Display display;
static CvOutput cvOutput;
static BpmClock bpmClock;

// --- Firmware version: shown for ~1s at boot ("M.mm") and persisted to QSPI so a
//     future build can read the previously-installed version (e.g. to migrate).
//     BUMP THIS on every flashed release. ---
static constexpr int      FW_VER_MAJOR = 1;
static constexpr int      FW_VER_MINOR = 13;  // 0..99, shown as two digits (v1.13)
static constexpr uint32_t FW_VERSION   = (uint32_t)FW_VER_MAJOR * 100u + FW_VER_MINOR; // 100 = v1.00

// --- Persisted settings (QSPI). Bump SETTINGS_VERSION to invalidate old layouts. ---
static constexpr uint32_t SETTINGS_VERSION = 15; // v15: CV Out slew split into independent rise/fall
struct OghamSettings {
    uint32_t version;
    int out1Formula;
    int out2Formula;
    FxChainConfig fx;   // 3-stage FX chain (mixes + flavour params + topology)
    // Out2 drone frozen state (daisy-pcq), meaningful only when fx.out2Drone: the
    // exact phase increment (rate) + A/B captured at decouple, so the drone's pitch
    // and sound survive a power cycle instead of re-snapshotting live boot state.
    uint64_t droneInc;
    int32_t  droneA;
    int32_t  droneB;
    uint32_t fwVersion;   // firmware version that last wrote these settings (FW_VERSION)
    bool operator!=(const OghamSettings& o) const {
        return version != o.version
            || out1Formula != o.out1Formula
            || out2Formula != o.out2Formula
            || memcmp(&fx, &o.fx, sizeof(FxChainConfig)) != 0
            || droneInc != o.droneInc
            || droneA != o.droneA
            || droneB != o.droneB
            || fwVersion != o.fwVersion;   // so a version-only change actually persists
    }
    bool operator==(const OghamSettings& o) const { return !(*this != o); }
};
static PersistentStorage<OghamSettings> storage(hw.qspi);
static bool settingsDirty = false;
static uint32_t lastSettingChangeMs = 0;
static constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 3000;  // debounce flash writes

// --- Telemetry for the PC monitor (read live over SWD; daisy-emp). ---
// All fields are 4 bytes for a predictable, padding-free layout. `volatile`
// keeps the writes alive (nothing on-device reads this struct).
struct Telemetry {
    uint32_t magic;        // 0x4F474841 ("OGHA")
    uint32_t counter;      // increments each loop (liveness)
    uint32_t segs;         // 4 TM1637 seg bytes, LE: seg0 | seg1<<8 | seg2<<16 | seg3<<24
    uint32_t modeState;    // funcMode | selOut<<8 | lofiClean<<16 | extClockActive<<24
    uint32_t ioState;      // gateIn | clockIn<<8
    int32_t  out1Formula;
    int32_t  out2Formula;
    int32_t  paramA;
    int32_t  paramB;
    float    rate;
    float    extClockRate;
    float    potA;         // raw ADC0
    float    potB;         // raw ADC1
    float    potRate;      // raw ADC2
    float    potLevel;     // raw ADC3
    float    combinedA;    // 0-1 (pot+CV, MCP6004)
    float    combinedB;    // 0-1
    float    cvA;          // raw ADC4 (CV_A pin)
    float    cvB;          // raw ADC5 (CV_B pin)
    uint32_t cpuPeak;      // audio-callback peak work (cycles)
    uint32_t cpuPeriod;    // audio block period (cycles)
    uint32_t syncCount;    // gate hard-sync (EXTI) events
    int32_t  fxType;       // selected post-Lo-Fi effect (FxType; 0 = Off)
};
volatile Telemetry g_telemetry;
volatile uint32_t g_syncCount = 0;   // incremented by the gate EXTI ISR

// --- Timing ---
static uint32_t lastDisplayTime = 0;
static constexpr uint32_t DISPLAY_INTERVAL_MS = 33; // ~30Hz

// --- Func (encoder) state machine ---
// SELECT: turn = formula, short-click = switch voice.
// FX menu: turn = cycle field; click = enter edit (value flashes); turn = edit
//          value; click = commit + back to navigate.
// Long-press enters FX from SELECT, and exits FX from anywhere (incl. editing).
enum FuncMode { FUNC_SELECT = 0, FUNC_FX = 1 };
static FuncMode funcMode = FUNC_SELECT;
static int  selOut = 0;       // voice the encoder edits in SELECT mode (0=Out1, 1=Out2)
// Current FX field (0..FX_NUM_FIELDS-1). Survives leaving and re-entering the
// menu so you land back where you were; zero-initialised, so a power cycle
// resets it to the first field. Not persisted to QSPI, by design.
static int  fxField = 0;
static bool fxEditing = false; // FX menu: false = navigate fields, true = edit value

// Live FX-chain config (the 0..99 source of truth; loaded from / saved to QSPI).
static FxChainConfig g_fx;

// Pointer to a stage param field's value (fields 2..13). Order MUST match
// FxChainConfig, Display::ShowFxEdit, and the menu. Returns nullptr for the
// global on/off (0) and the chain toggle (1).
static uint8_t* FxFieldPtr(FxChainConfig& f, int field) {
    switch (field) {
        case 2:  return &f.chorusLevel;  case 3:  return &f.chorusType;  case 4:  return &f.chorusP1;  case 5:  return &f.chorusP2;
        case 6:  return &f.flangerLevel; case 7:  return &f.flangerType; case 8:  return &f.flangerP1; case 9:  return &f.flangerP2;
        case 10: return &f.phaserLevel;  case 11: return &f.phaserType;  case 12: return &f.phaserP1; case 13: return &f.phaserP2;
        default: return nullptr;
    }
}

// True for the per-stage "type" sub-field (sub == 1): fields 3, 7, 11.
static inline bool FxFieldIsType(int field) {
    return field >= 2 && field <= 13 && ((field - 2) % 4) == 1;
}

// Param-interp grid (q) steps through this discrete list (0 = off). dir = turn.
static uint8_t NextQuant(uint8_t cur, int dir) {
    static const uint8_t list[] = {0, 4, 8, 16, 32, 64, 128};
    const int n = (int)(sizeof(list) / sizeof(list[0]));
    int idx = 0;
    for (int i = 0; i < n; i++) if (list[i] == cur) { idx = i; break; }
    idx += (dir > 0) ? 1 : -1;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return list[idx];
}

// Encoder gesture tracking (short click / long press / double click)
static bool encWasPressed = false;
static uint32_t encPressStart = 0;
static bool encLongFired = false;
static constexpr uint32_t LONG_PRESS_MS = 600;  // hold threshold to toggle delay mode

// Encoder acceleration: the faster you turn, the bigger the step (rapid travel
// through formulas/delay); slow turns stay 1-per-detent for fine control.
static uint32_t lastEncMs = 0;
static constexpr uint32_t ENC_FAST_MS = 25;   // detent gap below this -> fastest
static constexpr uint32_t ENC_MED_MS  = 50;
static constexpr uint32_t ENC_SLOW_MS = 90;
// Acceleration multipliers (tuned for the 100-slot function range): a hard crank
// jumps ~8/detent (~12 detents across the whole range), while single/slow detents
// stay 1-for-1 for precise landing. No-wrap clamps hard cranks at F0 / AA.
static constexpr int ENC_FAST_MULT = 8;
static constexpr int ENC_MED_MULT  = 4;
static constexpr int ENC_SLOW_MULT = 2;
// Menu-navigation acceleration is deliberately GENTLER than the function
// selector's 8/4/2. The menu is 20 fields, not 100 slots: at x8 a single fast
// detent would cross 40% of the list and overshooting would be the norm. x3
// still slams end-to-end in ~7 detents (and clamping makes those ends a
// reliable landing spot), while anything slower than a brisk turn stays exactly
// one field per detent so you can always land on the one you want.
static constexpr int ENC_MENU_FAST_MULT = 3;   // detent gap < ENC_FAST_MS
static constexpr int ENC_MENU_MED_MULT  = 2;   // detent gap < ENC_MED_MS

// --- POT_RATE calibration (raw ADC; pots non-linear from the 10k pull-down,
//     daisy-uzd). Center-aware so 12 o'clock = 1x rate. Recalibrated 2026-07-28
//     across the fleet (modules 1/2/4/5): 12 o'clock read 459/476/450/450 (mean
//     459) and full-CW ~959; the old 0.371 center was a stale single-unit value
//     that made noon play ~1.3x on every production module. ---
static constexpr float RATE_POT_MIN    = 0.0f;
static constexpr float RATE_POT_CENTER = 0.459f;   // fleet mean 12 o'clock
static constexpr float RATE_POT_MAX    = 0.955f;   // fleet full-CW ~0.959, margin so all reach 4x
// Raw-pot movement needed to re-roll CV Out's sample offset (daisy-*). ~100x
// the measured ADC noise (~1e-4) so it never self-triggers while stationary,
// but only ~2 degrees of a 300-degree throw, so a deliberate nudge always lands.
static constexpr float RATE_REROLL_DEADBAND = 0.008f;
static float lastRerollPot = -1.0f;   // <0 = not yet adopted (see the use site)

// --- CV->Timbre routing (daisy-gtw) ---
// Isolate the CV on the A/B jack by partitioning the summed pot+CV signal:
//   knob = K * GetPot(); cvOnly = GetCombined - knob.
// K matches the raw-pot ADC (ADC0/1) to the combined units (ADC4/5) so an
// UNPATCHED input reads cvOnly ~= 0 across the knob sweep. Per-board; tuned on
// hardware via telemetry (combined vs pot, no cable). Isolation is approximate
// (the two ADC paths have different curves) -- fine for a timbre macro.
static constexpr float TIMBRE_CV_K_A    = 1.3164f; // knob->combined gain match, ch A (cal 2026-07-24)
static constexpr float TIMBRE_CV_K_B    = 1.3271f; // ch B (cal 2026-07-24; residual ~0/255 both)
static constexpr float TIMBRE_CV_DEPTH  = 1.0f;   // isolated CV -> timbre-macro offset scale

// Map a raw pot reading to a centered 0..1 (calibrated center -> 0.5) so a
// downstream symmetric mapping puts its midpoint at the mechanical 12 o'clock.
static inline float CenterNorm(float raw, float lo, float ctr, float hi) {
    float n;
    if (raw <= ctr) n = (ctr > lo) ? 0.5f * (raw - lo) / (ctr - lo) : 0.0f;
    else            n = (hi > ctr) ? 0.5f + 0.5f * (raw - ctr) / (hi - ctr) : 1.0f;
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    return n;
}

// Map a centered pot (0..1, 0.5 = calibrated noon) to a clock ratio exponent in
// [-CLOCK_RATIO_MAX_EXP .. +CLOCK_RATIO_MAX_EXP]; noon = 0 (x1). The ratio is
// then 2^exp: powers-of-two multiply (CW) / divide (CCW) of the clock rate.
static constexpr int CLOCK_RATIO_MAX_EXP = 5;   // 2^5 = x32 up, /32 down
static inline int ClockRatioExp(float centered) {
    int e = (int)lroundf((centered - 0.5f) * (2 * CLOCK_RATIO_MAX_EXP));
    if (e >  CLOCK_RATIO_MAX_EXP) e =  CLOCK_RATIO_MAX_EXP;
    if (e < -CLOCK_RATIO_MAX_EXP) e = -CLOCK_RATIO_MAX_EXP;
    return e;
}

// --- BPM change detection ---
static int prevFormulaIdx = -1;
static int32_t prevParamA = 0;
static int32_t prevParamB = 0;

// --- External clock detection (hardware-timed in the clock EXTI ISR) ---
// volatile: written by the EXTI ISR, read by the main loop.
static volatile bool extClockActive = false;
static volatile float extClockRate = 1.0f;
static volatile uint32_t lastClockEdgeUs = 0;
static volatile uint32_t lastClockPeriodUs = 0;  // last median clock period (ISR -> main loop)
static constexpr uint32_t EXT_CLOCK_TIMEOUT_US = 2000000;  // 2 seconds (ISR per-edge gap)
// Adaptive revert-to-knob timeout: scale to the clock's own period so fast clocks
// revert almost immediately when unplugged, while slow clocks keep a wide enough
// window not to false-revert between their beats. (True instant detection would
// need the jack's switch contact wired to a GPIO -- not on this board; see daisy-*.)
static constexpr uint32_t EXT_CLOCK_TIMEOUT_PERIODS = 2;     // revert after ~2 missed beats
static constexpr uint32_t EXT_CLOCK_TIMEOUT_MIN_MS  = 90;    // floor (ignore fast-clock jitter)
static constexpr uint32_t EXT_CLOCK_TIMEOUT_MAX_MS  = 2000;  // ceiling (slow clocks)
static uint32_t lastSeenEdgeUs = 0;   // main-loop mirror of lastClockEdgeUs (edge-change detect)
static uint32_t lastEdgeSeenMs = 0;   // GetNow() ms when a new edge was last observed

// --- Clock freeze / hold-on-pull (daisy-79d) ---
// When a running clock's edges suddenly stop (cable pulled), HOLD the last clock
// rate instead of reverting to the knob (extClockRate stays frozen -- the ISR is
// no longer updating it). A later edge re-anchors (ISR re-sets extClockActive);
// moving the Rate/Fine pot past a deadband exits the hold to continuous Hz.
// Not persisted -> always false at boot (no stale hold).
static bool  clockHeld        = false;  // holding the last clock rate after edges stopped
static float heldRateRef      = 0.0f;   // raw Rate pot captured at hold entry (exit-on-move)
static int   lastClockRatioExp = 999;   // last shown ratio exponent (flash-on-change; 999 = none)
static constexpr float RATE_HOLD_EXIT_EPS = 0.02f;  // ~5 LSB of 255: pot move that exits a hold

// --- Clock In -> tempo (clocks the bytebeat's playback rate) ---
// The clock frequency maps ratiometrically to a playback-rate multiplier (then
// * the Rate knob). Pitch is intentionally NOT handled here (a future dedicated
// V/oct CV input owns pitch); Clock In is purely a tempo/clock source.
static constexpr float TEMPO_UNITY_HZ     = 8.0f;     // clock Hz that maps to 1x rate
static constexpr float CLOCK_RATE_MAX     = 64.0f;    // clamp runaway at fast clocks
static constexpr uint32_t MIN_CLOCK_PERIOD_US = 200;  // refractory: reject edges <200us apart

// --- V/oct pitch (daisy-c5i): hard-sync the engine at f_pitch from the V/oct CV. ---
// f_adc (0..~0.97) -> octaves -> f_pitch = base * 2^octaves. Nominal from the 10k/18k
// divider (~0.1948 ADC-fraction per volt = per octave); CALIBRATE on hardware.
static constexpr float VOCT_ZERO_FRAC    = 0.001f;  // V/oct ADC fraction at 0V (~P001)
static constexpr float VOCT_FRAC_PER_OCT = 0.1948f; // ADC fraction per octave (per 1V)
// The codec runs slightly fast vs the assumed 48kHz: the A440 calibration found
// base 28154 reads 440.0 where 28160 is nominal, i.e. real pitch = nominal * 1.000213.
// Divide requested pitches by this so they land on true frequencies.
static constexpr float SR_CORRECTION     = 28160.0f / 28154.0f;    // ~1.000213
static constexpr float VOCT_BASE_HZ      = 32.70f / SR_CORRECTION; // 0V + centered knob = true C1
// In V/oct mode the Rate knob is repurposed as a BIPOLAR FINE-TUNE offset
// (12 o'clock = 0, +-VOCT_KNOB_SPAN_OCT/2 octaves); the V/oct CV adds 1V/oct on
// top. Base (C1) so 0-5V CV covers C1..C6. Trade-off: timbre is fixed.
static constexpr float VOCT_KNOB_SPAN_OCT     = 2.0f;  // full knob span (+-1 oct = +-1V fine tune)
static constexpr float VOCT_FIXED_TIMBRE_RATE = 1.0f;  // fixed t-advance (timbre) in V/oct

// Median-3 period filter: rejects a single spurious/missed edge.
static uint32_t clkP0 = 0, clkP1 = 0, clkP2 = 0;
static inline uint32_t Median3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t t;
    if (a > b) { t = a; a = b; b = t; }
    if (b > c) { t = b; b = c; c = t; }
    if (a > b) { t = a; a = b; b = t; }
    return b;
}

// --- Audio callback (runs in DMA ISR context at 48kHz) ---
// --- CPU-load instrumentation (DWT cycle counter; daisy-5wx) ---
// load = work cycles / block-period cycles (clock- & block-size-independent).
volatile uint32_t g_cbWorkPeak = 0;   // peak callback work (cycles)
volatile uint32_t g_cbWorkLast = 0;   // last callback work (cycles)
volatile uint32_t g_cbPeriod   = 0;   // cycles between callback starts (block period)
static uint32_t   s_lastCbStart = 0;

// Interim audio-output attenuation (daisy-qqa). The analog TL072 stage (4.7x, as designed)
// is ~2x over-gained: full-scale is ~19.5Vpp (measured via a full-scale square: 7.2Vpp at
// 0.37x -> 19.5 at 1.0x, un-clipped) vs the ~10Vpp Eurorack level. Scale ~0.51 -> ~10Vpp
// full-scale. Set back to 1.0f when the hardware gain is halved.
static constexpr float AUDIO_OUT_LEVEL = 0.51f;

static void AudioCallback(AudioHandle::InputBuffer in,
                           AudioHandle::OutputBuffer out,
                           size_t size) {
    uint32_t cyc0 = DWT->CYCCNT;
    g_cbPeriod = cyc0 - s_lastCbStart;
    s_lastCbStart = cyc0;

    // Encoder scanning normally runs on TIM5 at 10kHz (daisy-szs). This 1kHz
    // fallback only engages if that timer failed to start -- sampling from BOTH
    // would double-count every detent.
    if (!encTimerRunning) controls.SampleEncoder();

    pipeline.Process(engine, out, size);

    // BPM clock from the clean (pre-Lo-Fi) signal for stable analysis; envelope
    // follower from the processed (post-Lo-Fi) Out1 so it tracks the audible output.
    const float* clean  = pipeline.GetCleanBuffer();
    const float* clean2 = pipeline.GetCleanBuffer2();
    const bool*  cap1   = pipeline.GetCvCaptureBuffer();
    const bool*  cap2   = pipeline.GetCvCaptureBuffer2();
    const float* holdSamp1 = pipeline.GetHoldSampleBuffer();
    const float* holdSamp2 = pipeline.GetHoldSampleBuffer2();
    for (size_t i = 0; i < size; i++) {
        // Envelope follower + BPM read the FULL-scale audio first (so ENV Out keeps
        // its range), then the audio jacks are attenuated below.
        cvOutput.ProcessSample(out[0][i], out[1][i], clean[i], clean2[i],
                               holdSamp1[i], holdSamp2[i], cap1[i], cap2[i]);
        bpmClock.ProcessSample(clean[i]);
        // Interim gain fix (daisy-qqa): the analog stage runs ~2x hot (~19.4Vpp);
        // halve the digital output to land at the ~10Vpp Eurorack level. Revert to
        // 1.0 once the hardware gain is halved. ENV Out is unaffected (read above).
        out[0][i] *= AUDIO_OUT_LEVEL;
        out[1][i] *= AUDIO_OUT_LEVEL;
    }

    uint32_t work = DWT->CYCCNT - cyc0;
    g_cbWorkLast = work;
    if (work > g_cbWorkPeak) g_cbWorkPeak = work;
}

// Gate EXTI: external gate rising -> PD2 falling -> request a hard-sync reset,
// and pluck the internal LPG (daisy-nmr) off the same edge. Both only raise a
// flag that the audio loop consumes at the next sample, so the waveform restart
// and the gate's attack land together, sample-accurate.
extern "C" void EXTI2_IRQHandler(void) {
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_2) != 0U) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_2);
        engine.SyncReset();
        pipeline.LpgTrigger();
        g_syncCount++;
    }
}

// Clock EXTI (CLK_IN = PC12, EXTI line 12): the LM393/GateIn inversion makes an
// external rising edge a FALLING edge on the pin. Timestamping the period in this
// hardware ISR (instead of polling the main loop, which goes blind ~9ms during
// each TM1637 write) gives stable audio-rate pitch tracking (daisy-... fix).
extern "C" void EXTI15_10_IRQHandler(void) {
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_12) == 0U) return;
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12);

    uint32_t now = System::GetUs();
    // Signed difference so the GetUs() rollover (it counts 0..~17.9M then wraps)
    // doesn't underflow into a huge "gap" -> false timeout -> dropout (audible as
    // a jump at low clock rates, where re-acquire takes a couple of edges).
    int32_t period = (int32_t)(now - lastClockEdgeUs);
    if (period < 0) {                               // GetUs wrapped: re-anchor, skip
        lastClockEdgeUs = now;
        return;
    }
    if (period < (int32_t)MIN_CLOCK_PERIOD_US) return;  // refractory: comparator chatter
    lastClockEdgeUs = now;                           // re-anchor on every accepted edge
    if (period >= (int32_t)EXT_CLOCK_TIMEOUT_US) {  // first edge after a real long gap:
        extClockActive = false;                      // re-anchor only; next edge tracks
        return;
    }
    uint32_t per = (uint32_t)period;

    // Median-3 (seed on the first edge after a gap, else shift in).
    if (!extClockActive) { clkP0 = clkP1 = clkP2 = per; extClockActive = true; }
    else                 { clkP0 = clkP1; clkP1 = clkP2; clkP2 = per; }
    uint32_t medP = Median3(clkP0, clkP1, clkP2);
    lastClockPeriodUs = medP;  // expose to the main-loop adaptive timeout

    // Tempo: snap the rate to the measured clock (no portamento). Knob trims it.
    float target = (1000000.0f / (float)medP) / TEMPO_UNITY_HZ;
    if (target > CLOCK_RATE_MAX) target = CLOCK_RATE_MAX;
    extClockRate = target;
}

int main(void) {
    // --- Initialize Daisy Seed hardware ---
    hw.Init();

    // --- Configure 7 ADC channels (pots + CV + V/oct) ---
    AdcChannelConfig adcConfig[ogham::NUM_ADC_CHANNELS];
    adcConfig[0].InitSingle(hw.GetPin(ogham::POT_A));
    adcConfig[1].InitSingle(hw.GetPin(ogham::POT_B));
    adcConfig[2].InitSingle(hw.GetPin(ogham::POT_RATE));
    adcConfig[3].InitSingle(hw.GetPin(ogham::POT_LEVEL));
    adcConfig[4].InitSingle(hw.GetPin(ogham::CV_A));
    adcConfig[5].InitSingle(hw.GetPin(ogham::CV_B));
    adcConfig[6].InitSingle(hw.GetPin(ogham::VOCT_ADC));
    hw.adc.Init(adcConfig, ogham::NUM_ADC_CHANNELS);

    // --- Configure DAC for CV output ---
    DacHandle::Config dacConfig;
    dacConfig.mode = DacHandle::Mode::POLLING;
    dacConfig.bitdepth = DacHandle::BitDepth::BITS_12;
    dacConfig.chn = DacHandle::Channel::ONE;
    hw.dac.Init(dacConfig);

    // --- Configure encoder ---
    encoder.Init(hw.GetPin(ogham::ENC_A),
                 hw.GetPin(ogham::ENC_B),
                 hw.GetPin(ogham::ENC_SW));

    // --- Configure gate input (active-high after MMBT3904 inversion) ---
    gateIn.Init(hw.GetPin(ogham::GATE_IN));

    // Gate hard-sync: EXTI interrupt on GATE_IN (PD2). Q1 inverts the signal, so
    // an external gate RISING edge is a FALLING edge on the pin. The ISR requests
    // a sample-accurate reset in the engine (vs the jittery main-loop poll).
    {
        __HAL_RCC_SYSCFG_CLK_ENABLE();   // required for the EXTI line->port mapping
        GPIO_InitTypeDef g = {};
        g.Pin  = GPIO_PIN_2;
        g.Mode = GPIO_MODE_IT_FALLING;
        g.Pull = GPIO_PULLUP;   // GATE_IN needs the internal pull-up (Q1 collector)
        HAL_GPIO_Init(GPIOD, &g);
        HAL_NVIC_SetPriority(EXTI2_IRQn, 3, 0);
        HAL_NVIC_EnableIRQ(EXTI2_IRQn);
    }

    // --- Configure clock input (active-high after LM393 comparator) ---
    clockIn.Init(hw.GetPin(ogham::CLK_IN));   // still read via State() for telemetry

    // Clock pitch: EXTI on CLK_IN (PC12, line 12). Like the gate, the inverting
    // input stage makes an external rising edge a FALLING edge on the pin. The
    // ISR times the period in hardware -> stable pitch (no main-loop/display
    // jitter). SYSCFG clock was enabled above for the gate EXTI.
    {
        GPIO_InitTypeDef c = {};
        c.Pin  = GPIO_PIN_12;
        c.Mode = GPIO_MODE_IT_FALLING;
        c.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOC, &c);
        HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3, 0);
        HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    }

    // --- Configure gate output ---
    gateOutGpio.Init(hw.GetPin(ogham::GATE_OUT),
                     GPIO::Mode::OUTPUT,
                     GPIO::Pull::NOPULL,
                     GPIO::Speed::LOW);
    gateOutGpio.Write(false);

    // --- Initialize modules ---
    engine.Init();
    pipeline.Init();
    controls.Init(&hw, &encoder, &gateIn, &clockIn);
    tm1637.Init(&hw, ogham::TM1637_CLK, ogham::TM1637_DIO);
    display.Init(&tm1637);
    cvOutput.Init(&hw.dac);
    bpmClock.Init();
    bpmClock.RequestEstimate();

    // --- Encoder scan timer (daisy-szs): TIM5 @ 10kHz ---
    // Must follow controls.Init() (which configures the A/B pins and seeds the
    // decoder's previous state). TIM2 belongs to libDaisy's System clock; TIM5
    // is free on this board. If either call fails, encTimerRunning stays false
    // and the audio callback keeps scanning at 1kHz as before.
    {
        TimerHandle::Config cfg;
        cfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
        cfg.dir        = TimerHandle::Config::CounterDir::UP;
        cfg.enable_irq = true;
        // APB1 timer kernel clock = PCLK1 * 2 (NOT PCLK2 -- see ENC_SCAN_HZ).
        cfg.period     = (HAL_RCC_GetPCLK1Freq() * 2) / ENC_SCAN_HZ;
        if (encTim.Init(cfg) == TimerHandle::Result::OK) {
            encTim.SetCallback(EncoderScanCallback);
            encTimerRunning = (encTim.Start() == TimerHandle::Result::OK);
        }
    }

    // --- Persistence: load saved settings (or factory defaults) from QSPI ---
    // Both voices = first formula; FX chain = module default.
    OghamSettings defaults = { SETTINGS_VERSION, 0, 0, AudioPipeline::DefaultFxChain(),
                               0, 128, 128, FW_VERSION };  // drone inc=0, A/B mid; fw stamp
    storage.Init(defaults);
    if (storage.GetSettings().version != SETTINGS_VERSION) {
        storage.RestoreDefaults();  // stored layout is from an old version -> wipe
    }

    // Capture the previously-stored firmware version (available for a future
    // migration). The running version is stamped + persisted LATER, via the normal
    // debounced main-loop save -- NOT with an inline storage.Save() here: a blocking
    // QSPI erase/write this early in boot (before the display splash even starts)
    // stalled the boot on the first update that triggered it. See the stamp after
    // settings are applied.
    uint32_t prevFwVersion = storage.GetSettings().fwVersion;
    (void)prevFwVersion;   // no version-keyed migrations yet

    // --- Enable DWT cycle counter for CPU-load measurement (daisy-5wx) ---
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // --- Start the ADC (audio is started later, once the params are stable) ---
    hw.adc.Start();

    // --- Startup splash (~1s): a single segment chases the outer border for 4
    //     laps. controls.Process() runs in the gaps to debounce the encoder and
    //     warm the pot ADC. Audio is NOT running yet, so there's no startup noise. ---
    {
        const int N = 12, STEPS = N * 4;    // 4 laps
        const uint32_t STEP_MS = 21;         // ~1s total (48 * 21ms)
        for (int s = 0; s < STEPS; s++) {
            uint32_t t0 = System::GetNow();
            tm1637.ShowBorderSegment(s % N);
            while (System::GetNow() - t0 < STEP_MS) {
                controls.Process();
                System::Delay(1);            // ~1kHz for the encoder debounce
            }
        }
        tm1637.Clear();
    }

    // --- Firmware version (~1s): show "M.mm" (e.g. " 1.00") so the installed build
    //     is identifiable at power-on and after a future flash update. controls
    //     keeps processing so a held encoder still arms the factory reset below. ---
    {
        display.ShowVersion(FW_VER_MAJOR, FW_VER_MINOR);
        uint32_t t0 = System::GetNow();
        while (System::GetNow() - t0 < 1000) {
            controls.Process();
            System::Delay(1);
        }
        tm1637.Clear();
    }

    // Snap the pot/CV smoothers to the actual readings -> stable params right now.
    controls.PrimeSmoothing();

    // --- Factory reset: hold the Func encoder at power-on to restore defaults
    //     (both voices on the first formula, delay 0). The encoder is already
    //     debounced by the splash above. ---
    if (controls.GetEncoderPressed()) {
        storage.RestoreDefaults();
    }

    // Consume the power-on hold so the main-loop gesture detector doesn't see
    // the already-held encoder as a fresh press (which would fire a long-press
    // and drop straight into FX mode).
    encWasPressed = controls.GetEncoderPressed();
    encLongFired  = true;

    // --- Apply the loaded / default / reset settings to the engine + FX chain ---
    {
        OghamSettings& s = storage.GetSettings();
        engine.SetFormula1(s.out1Formula);
        engine.SetFormula2(s.out2Formula);
        g_fx = s.fx;
        pipeline.SetFxChain(g_fx);
        engine.SetParamQuant(g_fx.paramQuant);  // engine-side; not via the pipeline
        // Restore a decoupled Out2 drone from its persisted frozen state (exact rate
        // + A/B), rather than letting the per-loop toggle re-snapshot live boot state
        // (which shifted the drone's pitch/sound across power cycles).
        if (g_fx.out2Drone) {
            engine.RestoreOut2Drone(s.droneInc, s.droneA, s.droneB);
        }
        // Stamp the running firmware version and let the normal debounced save
        // persist it (proven runtime path -- no fragile early-boot QSPI write).
        if (s.fwVersion != FW_VERSION) {
            s.fwVersion         = FW_VERSION;
            settingsDirty       = true;
            lastSettingChangeMs = System::GetNow();
        }
    }

    // Seed the shared params from the (now-stable) pot/CV readings, so audio starts
    // with the correct values (no startup noise) and the display doesn't flash at boot.
    {
        int32_t a = (int32_t)(controls.GetCombinedA() * 255.0f + 0.5f);
        int32_t b = (int32_t)(controls.GetCombinedB() * 255.0f + 0.5f);
        a = (a < 0) ? 0 : (a > 255 ? 255 : a);
        b = (b < 0) ? 0 : (b > 255 ? 255 : b);
        engine.SetParamA(a);
        engine.SetParamB(b);
    }

    // --- Start audio now that the params are stable ---
    hw.StartAudio(AudioCallback);

    // --- Let the encoder scan preempt audio (daisy-072) -------------------
    // Must come AFTER StartAudio: libDaisy assigns the DMA priorities during
    // audio init, so anything set earlier is overwritten.
    //
    // libDaisy gives TIM2-5 the LOWEST NVIC priority (0x0f, per/tim.cpp:293)
    // while every DMA stream sits at 0 (sys/dma.c), so the audio callback
    // blocked the encoder scan for its entire duration. Measured on module 2:
    // at 19.7% load the scan held its full 20kHz with a worst gap of one
    // period, but at 48.1% the effective rate collapsed to 11.2kHz, 9% of
    // scans were delayed past 250us, and the worst gap was 549us. Starved that
    // far, a brisk turn (>1.6 rev/s) crosses two quadrature states between
    // scans; QDEC decodes an ambiguous transition as 0, so the movement is
    // DISCARDED rather than deferred. The same brisk gesture decoded 21 detents
    // at low load and 6 at high -- a 71% loss -- and left encSubAccum_ at -2,
    // so the damage outlived the gesture.
    //
    // Audio still has effective precedence: this trades latency, not
    // throughput. The scan ISR is ~50 cycles, so 20kHz of it costs ~0.25% of
    // the core and the callback keeps ~50% slack -- it gets interrupted
    // briefly, it cannot miss its deadline. Preempt priority cannot go below 0,
    // so the SAI DMA streams move to 1 and TIM5 takes 0. Streams 0/1 are SAI1
    // and 3/4 are SAI2 (libDaisy sai.cpp); all four are set so this holds
    // whichever SAI the board brings up.
    if (encTimerRunning) {
        static const IRQn_Type kSaiDmaIrqs[] = {
            DMA1_Stream0_IRQn, DMA1_Stream1_IRQn,
            DMA1_Stream3_IRQn, DMA1_Stream4_IRQn };
        for (IRQn_Type irq : kSaiDmaIrqs) HAL_NVIC_SetPriority(irq, 1, 0);
        HAL_NVIC_SetPriority(TIM5_IRQn, 0, 0);
    }

    // Belt-and-suspenders: keep the param-flash muted for a short grace at boot.
    uint32_t bootMs = System::GetNow();
    static constexpr uint32_t PARAM_FLASH_GRACE_MS = 600;

    // --- Main loop ---
    while (1) {
        // Read controls (~1kHz)
        controls.Process();

        // --- Func encoder: gestures + state machine ---
        {
            uint32_t nowMs = System::GetNow();
            bool pressed = controls.GetEncoderPressed();
            bool gShort = false, gLong = false;

            if (pressed && !encWasPressed) {            // press start
                encPressStart = nowMs;
                encLongFired = false;
            }
            if (pressed && !encLongFired &&
                (nowMs - encPressStart >= LONG_PRESS_MS)) {
                encLongFired = true;
                gLong = true;                            // long press (while held)
            }
            if (!pressed && encWasPressed && !encLongFired) {  // short release (not a long press)
                gShort = true;
            }
            encWasPressed = pressed;

            // Long press: enter FX from SELECT, or exit FX from anywhere
            // (including mid-edit). Short click: SELECT -> switch voice;
            // FX -> toggle navigate <-> edit on the current field.
            if (gLong) {
                if (funcMode == FUNC_FX) {
                    funcMode = FUNC_SELECT;
                    fxEditing = false;
                } else {
                    funcMode = FUNC_FX;
                    // Re-enter on the field you left, NOT field 0: with 20 fields
                    // and no wrap, re-navigating from the top every time to tweak
                    // one parameter is the whole cost of the menu. RAM only --
                    // fxField is zero-initialised, so a power cycle starts at 0
                    // again. Deliberately not persisted.
                    fxEditing = false;   // but never resume mid-edit
                }
            } else if (gShort) {
                if (funcMode == FUNC_SELECT) selOut ^= 1;
                else fxEditing = !fxEditing;
            }

            // Encoder turn (acceleration scales the step by how fast you turn).
            int enc = controls.GetEncoderIncrement();
            if (enc != 0) {
                uint32_t dt = nowMs - lastEncMs;
                lastEncMs = nowMs;
                int step = 1;
                if (dt < ENC_FAST_MS)      step = ENC_FAST_MULT;
                else if (dt < ENC_MED_MS)  step = ENC_MED_MULT;
                else if (dt < ENC_SLOW_MS) step = ENC_SLOW_MULT;
                int delta = enc * step;

                if (funcMode == FUNC_FX) {
                    if (!fxEditing) {
                        // Navigate: CLAMPED at both ends -- no wrap, matching the
                        // function selector. Wrapping made a crank at either end
                        // silently jump to the far side of the menu; clamping
                        // means the ends are findable by feel. Accelerated on its
                        // own gentler curve (see ENC_MENU_*_MULT), so a crank
                        // crosses the list but a deliberate turn steps one field.
                        int mstep = 1;
                        if (dt < ENC_FAST_MS)      mstep = ENC_MENU_FAST_MULT;
                        else if (dt < ENC_MED_MS)  mstep = ENC_MENU_MED_MULT;
                        int n = fxField + enc * mstep;
                        if (n < 0) n = 0;
                        if (n > FX_NUM_FIELDS - 1) n = FX_NUM_FIELDS - 1;
                        fxField = n;
                    } else if (fxField == FX_FIELD_GLOBAL) {
                        g_fx.enabled = (enc > 0) ? 1 : 0;   // CW = on, CCW = off
                        pipeline.SetFxChain(g_fx);
                    } else if (fxField == FX_FIELD_CHAIN) {
                        g_fx.parallel = (enc > 0) ? 1 : 0;  // CW = parallel, CCW = serial
                        pipeline.SetFxChain(g_fx);
                    } else if (fxField == FX_FIELD_QUANT) {
                        // Discrete cycle through {0,16,32,64,128}; engine setting.
                        g_fx.paramQuant = NextQuant(g_fx.paramQuant, enc);
                        engine.SetParamQuant(g_fx.paramQuant);
                    } else if (fxField == FX_FIELD_DRONE) {
                        // Out2 decouple/drone: CW = decoupled (frozen), CCW = coupled.
                        // The engine snapshots on the couple->decouple edge (below).
                        g_fx.out2Drone = (enc > 0) ? 1 : 0;
                    } else if (fxField == FX_FIELD_CVOUT) {
                        // CV-out mode: 0 Env1 / 1 Env2 / 2 DC Out1 / 3 DC Out2.
                        int nv = (int)g_fx.cvOutMode + (enc > 0 ? 1 : -1);
                        if (nv < 0) nv = 0;
                        if (nv > 3) nv = 3;
                        g_fx.cvOutMode = (uint8_t)nv;
                    } else if (fxField == FX_FIELD_TIMBRECV) {
                        // CV->Timbre routing: 0 normal / 1 CV A / 2 CV B.
                        int nv = (int)g_fx.timbreCvRoute + (enc > 0 ? 1 : -1);
                        if (nv < 0) nv = 0;
                        if (nv > 2) nv = 2;
                        g_fx.timbreCvRoute = (uint8_t)nv;
                    } else if (fxField == FX_FIELD_LPG) {
                        // Internal LPG, consolidated on/off + decay (daisy-*): 0 =
                        // off, 1..99 = on with that decay (2ms..20s exp curve).
                        // Accelerates like a param; SetFxChain plucks it once on
                        // the 0->nonzero edge so the change is audible.
                        int nv = (int)g_fx.lpgDecay + delta;
                        if (nv < 0) nv = 0;
                        if (nv > 99) nv = 99;
                        g_fx.lpgDecay = (uint8_t)nv;
                        pipeline.SetFxChain(g_fx);   // live preview
                    } else if (fxField == FX_FIELD_CVSLEWRISE) {
                        // CV Out slew, rising 0..99 (exp instant..CV_SLEW_MAX_S);
                        // accelerates like a param. Applied every loop via
                        // cvOutput.SetSlewRise() above.
                        int nv = (int)g_fx.cvSlewRise + delta;
                        if (nv < 0) nv = 0;
                        if (nv > 99) nv = 99;
                        g_fx.cvSlewRise = (uint8_t)nv;
                    } else if (fxField == FX_FIELD_CVSLEWFALL) {
                        // CV Out slew, falling -- same mapping/behaviour as rise,
                        // independent coefficient (daisy-*).
                        int nv = (int)g_fx.cvSlewFall + delta;
                        if (nv < 0) nv = 0;
                        if (nv > 99) nv = 99;
                        g_fx.cvSlewFall = (uint8_t)nv;
                    } else if (fxField == FX_FIELD_CVHOLD) {
                        // CV Out hold: 0 = off (every tick) .. 8 = every 256 ticks,
                        // power-of-2 steps -- one detent per step, not accelerated
                        // (only 9 values; matches CVOUT/TIMBRECV's single-step feel).
                        int nv = (int)g_fx.cvHold + (enc > 0 ? 1 : -1);
                        if (nv < 0) nv = 0;
                        if (nv > 8) nv = 8;
                        g_fx.cvHold = (uint8_t)nv;
                    } else {
                        // Type fields clamp to FX_TYPE_MAX; level/param use 0..99.
                        uint8_t* p = FxFieldPtr(g_fx, fxField);
                        int maxv = FxFieldIsType(fxField) ? FX_TYPE_MAX : 99;
                        int nv = (int)(*p) + delta;
                        if (nv < 0) nv = 0;
                        if (nv > maxv) nv = maxv;
                        *p = (uint8_t)nv;
                        pipeline.SetFxChain(g_fx);   // live preview; saved by debounce
                    }
                } else if (selOut == 0) {
                    engine.SetFormula1(engine.GetFormula1Index() + delta);
                } else {
                    engine.SetFormula2(engine.GetFormula2Index() + delta);
                }
            }
        }

        // CV->Timbre routing (daisy-gtw): partition each channel's summed pot+CV
        // into a knob part + isolated CV part. In the alt routing modes that CV is
        // borrowed to modulate the Timbre/Lo-Fi macro (below), and the channel's
        // Param then follows the knob only.
        int   timbreRoute = g_fx.timbreCvRoute;
        float knobA   = TIMBRE_CV_K_A * controls.GetPot(0);
        float knobB   = TIMBRE_CV_K_B * controls.GetPot(1);
        float cvOnlyA = controls.GetCombinedA() - knobA;
        float cvOnlyB = controls.GetCombinedB() - knobB;
        float paramASrc = (timbreRoute == 1) ? knobA : controls.GetCombinedA();
        float paramBSrc = (timbreRoute == 2) ? knobB : controls.GetCombinedB();
        if (paramASrc < 0.0f) paramASrc = 0.0f;
        if (paramASrc > 1.0f) paramASrc = 1.0f;
        if (paramBSrc < 0.0f) paramBSrc = 0.0f;
        if (paramBSrc > 1.0f) paramBSrc = 1.0f;

        // Combined Pot+CV -> Parameters A/B, fixed 0-255 range fed to BOTH
        // voices. No per-formula ranges or stored defaults: the pot position
        // always directly represents the current value, selecting a new formula
        // keeps the live pot values (max exploration, min hidden state), and the
        // shared params stay decoupled from formula selection.
        {
            // Raw-pot (knob) movement detection: the param flash is triggered by
            // the user TWISTING a knob, not by CV changing the value. GetPot()
            // reads ADC0/1 (pot alone), separate from GetCombinedA/B (pot+CV).
            static constexpr float KNOB_MOVE_EPS = 0.006f; // ~1.5 LSB of 255
            static float lastRawA = -1.0f, lastRawB = -1.0f;

            int32_t a = (int32_t)(paramASrc * 255.0f + 0.5f);
            if (a < 0) a = 0;
            if (a > 255) a = 255;
            // Params still track the pots in FX mode, but suppress the flash so it
            // doesn't clobber the FX menu (ADC jitter would otherwise hijack it).
            bool flashOk = (System::GetNow() - bootMs) > PARAM_FLASH_GRACE_MS
                        && funcMode != FUNC_FX;
            // 1-LSB deadband rejects ADC jitter, but always let the exact
            // endpoints (0/255) latch so the full range is reachable.
            int32_t curA = engine.GetParamA();
            bool endA = (a != curA) && (a == 0 || a == 255);
            if (a - curA > 1 || curA - a > 1 || endA) {
                engine.SetParamA(a);
                // Keep an active flash's value live (incl. CV) but DON'T restart
                // its timeout — so CV alone can't hold the display on.
                if (flashOk) display.UpdateFlashValue('A', a);
            }
            // (Re)start the flash only on physical knob movement; show the
            // combined (pot+CV) value once triggered.
            float rawA = controls.GetPot(0);
            if (rawA - lastRawA > KNOB_MOVE_EPS || lastRawA - rawA > KNOB_MOVE_EPS) {
                lastRawA = rawA;
                if (flashOk) display.FlashParam('A', a);
            }

            int32_t b = (int32_t)(paramBSrc * 255.0f + 0.5f);
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            int32_t curB = engine.GetParamB();
            bool endB = (b != curB) && (b == 0 || b == 255);
            if (b - curB > 1 || curB - b > 1 || endB) {
                engine.SetParamB(b);
                if (flashOk) display.UpdateFlashValue('b', b);
            }
            float rawB = controls.GetPot(1);
            if (rawB - lastRawB > KNOB_MOVE_EPS || lastRawB - rawB > KNOB_MOVE_EPS) {
                lastRawB = rawB;
                if (flashOk) display.FlashParam('b', b);
            }
        }

        // --- Clock hold state (daisy-79d) ---
        // A live clock overrides any hold (re-anchor on a late edge); while held,
        // moving the Rate/Fine pot past the deadband exits back to continuous Hz.
        if (extClockActive) {
            clockHeld = false;
        } else if (clockHeld) {
            float r = controls.GetRate();
            if (r - heldRateRef > RATE_HOLD_EXIT_EPS ||
                heldRateRef - r > RATE_HOLD_EXIT_EPS) {
                clockHeld = false;
            }
        }

        // Pot 3 -> Rate (exponential 1/64x - 64x), calibrated so 12 o'clock = 1x
        float rawRatePot = controls.GetRate();
        float rateKnob = CenterNorm(rawRatePot,
                                    RATE_POT_MIN, RATE_POT_CENTER, RATE_POT_MAX);
        float knobRate = Controls::MapKnobToRate(rateKnob);

        // Re-roll CV Out's sample offset whenever the Rate knob actually moves
        // (daisy-*). Which offset the Hold stage sits on decides how the capture
        // lines up with the formula's own periodicity, which changes how VARIED
        // the resulting LFO is far more than it changes its level -- and there's
        // no way to pick a good one in advance. Tying a re-roll to the knob makes
        // finding a good one a gesture: nudge Rate, get a different character,
        // and it then holds still until you nudge again.
        //
        // Keyed off the RAW pot, not the derived rate, so it still re-rolls in
        // clocked mode (where the knob quantises to x/div steps and small moves
        // wouldn't change the rate at all). The deadband is ~100x the measured
        // ADC noise (~1e-4) but still only ~2 degrees of a 300-degree throw, so
        // it can't self-trigger while stationary yet a small deliberate nudge
        // always lands.
        if (lastRerollPot < 0.0f) {
            // First control pass: adopt the knob position WITHOUT rolling, so a
            // power cycle comes back on the built-in offset and a saved patch
            // sounds the same as you left it. The first nudge randomises.
            lastRerollPot = rawRatePot;
        } else if (fabsf(rawRatePot - lastRerollPot) > RATE_REROLL_DEADBAND) {
            lastRerollPot = rawRatePot;
            pipeline.RerollCvSampleOffset(daisy::System::GetUs());
        }
        if (engine.GetFormula1Index() == GetReferenceIndex()) {
            // A440 reference (daisy-vu3): pin rate to exactly 1x so the tone is a
            // dead-accurate 440Hz, independent of the Rate knob / clock / V-oct.
            engine.SetPitchSync(0.0f);
            engine.SetRate(1.0f);
        } else if (controls.IsVoctMode()) {
            // V/oct: hard-sync pitch. Rate knob = bipolar fine tune (12 o'clock = 0,
            // +-VOCT_KNOB_SPAN_OCT/2 oct); the V/oct CV adds 1V/oct on top. Timbre is
            // fixed (knob is repurposed to pitch here). Clock ignored.
            float cvOct   = (controls.GetVoct() - VOCT_ZERO_FRAC) / VOCT_FRAC_PER_OCT;
            float knobOct = (rateKnob - 0.5f) * VOCT_KNOB_SPAN_OCT;
            float fpitch  = VOCT_BASE_HZ * powf(2.0f, cvOct + knobOct);
            engine.SetPitchSync(fpitch);
            engine.SetRate(VOCT_FIXED_TIMBRE_RATE);
        } else {
            // Clock In sets the playback rate. With a clock present (or held after
            // a cable-pull) the Rate/Fine pot is a QUANTIZED multiply/divide of the
            // clock rate (/32../2, x1 at noon, x2..x32); with no clock it's the
            // continuous 1/64x-64x knob (wide enough to run the engine at LFO-rate
            // speeds for CV Out). (daisy-79d)
            engine.SetPitchSync(0.0f);
            if (extClockActive || clockHeld) {
                // Live clock: pot picks the ratio. Held: freeze the last ratio
                // (moving the pot exits the hold instead of re-quantizing).
                int e = extClockActive ? ClockRatioExp(rateKnob)
                                       : (lastClockRatioExp == 999 ? 0 : lastClockRatioExp);
                float rate = extClockRate * ldexpf(1.0f, (float)e);  // 2^e, exact
                if (rate > CLOCK_RATE_MAX) rate = CLOCK_RATE_MAX;    // keep it sane at x32
                engine.SetRate(rate);
                // Flash the selected ratio while turning under a live clock:
                // multiply = bare number; divide = number with a top-bar divide hint.
                if (extClockActive && e != lastClockRatioExp) {
                    display.FlashClockRatio(e);
                }
                lastClockRatioExp = e;
            } else {
                engine.SetRate(knobRate);
                lastClockRatioExp = 999;   // reset so the next clock re-flashes x1
            }
        }

        // Pot 4 -> Lo-fi tone macro (12 o'clock = clean; processes both voices).
        // CV->Timbre routing (daisy-gtw): add the borrowed channel's isolated CV as
        // a bidirectional offset around the knob position (self-clamps in SetLofiMacro).
        float timbre = controls.GetLevel();
        if (timbreRoute == 1)      timbre += TIMBRE_CV_DEPTH * cvOnlyA;
        else if (timbreRoute == 2) timbre += TIMBRE_CV_DEPTH * cvOnlyB;
        if (timbre < 0.0f) timbre = 0.0f;
        if (timbre > 1.0f) timbre = 1.0f;
        pipeline.SetLofiMacro(timbre);

        // Out2 decouple/drone (daisy-pcq): apply the persisted toggle. Idempotent --
        // the engine only snapshots Out2 on the couple->decouple edge, so calling
        // every loop is safe and also applies the boot-restored state.
        engine.SetOut2Decoupled(g_fx.out2Drone != 0);

        // CV-out mode (daisy-0pq): env follower / DC Out1 / DC Out2.
        cvOutput.SetMode((CvOutput::Mode)g_fx.cvOutMode);
        // CV Out slew (rise/fall independent, all modes) + hold (DC modes only).
        // Capture interval first: the DC-mode slew coefficients are derived
        // from it, so a Rate change has to land before the slew values are
        // (re)applied. Both are no-ops unless something actually changed.
        cvOutput.SetCaptureInterval(pipeline.GetCaptureSamples(),
                                    pipeline.GetCaptureSamples2());
        cvOutput.SetSlewRise(g_fx.cvSlewRise);
        cvOutput.SetSlewFall(g_fx.cvSlewFall);
        cvOutput.SetHold(g_fx.cvHold);

        // --- Gate In: hard-sync reset is handled by the EXTI ISR (sample-
        //     accurate), not polled here. ---

        // --- External clock: edges are timed in the EXTI ISR (above). Here we
        //     only time out when edges stop, reverting to the Rate knob. ---
        // Measure the timeout on the MONOTONIC ms clock (GetNow, wraps ~49 days),
        // NOT GetUs: GetUs wraps every ~17.9s, and when the last edge landed in the
        // top ~2M us of that cycle the signed-diff timeout stayed <2e6-or-negative
        // forever -> extClockActive stuck true after the cable was pulled (the tempo
        // was "remembered"). Instead we watch the ISR's lastClockEdgeUs: while it
        // keeps changing, clock is present; once it stops, count ms since and revert.
        uint32_t edgeUs = lastClockEdgeUs;   // atomic 32-bit snapshot of the ISR value
        if (edgeUs != lastSeenEdgeUs) {
            lastSeenEdgeUs = edgeUs;
            lastEdgeSeenMs = System::GetNow();
        }
        // Adaptive timeout: ~2 clock periods, clamped. Fast clocks revert quickly
        // on unplug; slow clocks keep a wide window so they don't self-revert.
        uint32_t timeoutMs = (lastClockPeriodUs / 1000) * EXT_CLOCK_TIMEOUT_PERIODS;
        if (timeoutMs < EXT_CLOCK_TIMEOUT_MIN_MS) timeoutMs = EXT_CLOCK_TIMEOUT_MIN_MS;
        if (timeoutMs > EXT_CLOCK_TIMEOUT_MAX_MS) timeoutMs = EXT_CLOCK_TIMEOUT_MAX_MS;
        if (extClockActive && (System::GetNow() - lastEdgeSeenMs) > timeoutMs) {
            // Edges stopped: HOLD the last clock rate rather than reverting to the
            // knob (daisy-79d). extClockRate stays frozen since the ISR no longer
            // updates it; a later edge re-anchors, a Rate-pot move exits the hold.
            extClockActive = false;
            clockHeld      = true;
            heldRateRef    = controls.GetRate();   // raw pot ref for exit-on-move
        }

        // --- Update CV output (envelope follower) ---
        cvOutput.UpdateOutput();

        // --- BPM: re-estimate when formula or A/B changes ---
        {
            int idx = engine.GetFormula1Index();
            int32_t a = engine.GetParamA();
            int32_t b = engine.GetParamB();
            if (idx != prevFormulaIdx ||
                (a - prevParamA) > 3 || (prevParamA - a) > 3 ||
                (b - prevParamB) > 3 || (prevParamB - b) > 3) {
                prevFormulaIdx = idx;
                prevParamA = a;
                prevParamB = b;
                bpmClock.RequestEstimate();
            }
        }

        // Update BPM clock estimation + rate scaling
        bpmClock.Update(engine.GetRate());

        // Write BPM-synced clock to gate output GPIO
        gateOutGpio.Write(bpmClock.GetClockState());

        // --- Display update (~30Hz) ---
        uint32_t now = System::GetNow();
        if (now - lastDisplayTime >= DISPLAY_INTERVAL_MS) {
            lastDisplayTime = now;

            display.Update();  // time out any param flash first
            bool clean = pipeline.IsLofiClean();
            if (display.IsFlashing()) {
                // Deferred param flash: the (blocking) write happens here at 30Hz,
                // not in the per-loop param path (which would throttle the loop).
                display.DrawPendingFlash(clean);
            } else if (funcMode == FUNC_FX) {
                int val = 0;
                if (fxField == FX_FIELD_GLOBAL)      val = g_fx.enabled;
                else if (fxField == FX_FIELD_QUANT)  val = g_fx.paramQuant;
                else if (fxField == FX_FIELD_DRONE)  val = g_fx.out2Drone;
                else if (fxField == FX_FIELD_CVOUT)  val = g_fx.cvOutMode;
                else if (fxField == FX_FIELD_TIMBRECV) val = g_fx.timbreCvRoute;
                else if (fxField == FX_FIELD_LPG)      val = g_fx.lpgDecay;
                else if (fxField == FX_FIELD_CVSLEWRISE) val = g_fx.cvSlewRise;
                else if (fxField == FX_FIELD_CVSLEWFALL) val = g_fx.cvSlewFall;
                else if (fxField == FX_FIELD_CVHOLD)   val = (g_fx.cvHold == 0) ? 0 : (1 << g_fx.cvHold);
                else if (fxField != FX_FIELD_CHAIN)  val = *FxFieldPtr(g_fx, fxField);
                // Edit mode: flash the value at ~80% duty (~600ms period) so it's
                // clear you're editing vs navigating.
                bool blankValue = fxEditing && ((now % 600) >= 480);
                display.ShowFxEdit(fxField, val, g_fx.parallel != 0, clean, blankValue);
            } else {
                int idx = (selOut == 0) ? engine.GetFormula1Index()
                                        : engine.GetFormula2Index();
                if (idx == GetReferenceIndex()) {
                    display.ShowVoiceRef(selOut + 1, clean);   // "X-AA" A440 reference
                } else {
                    display.ShowVoice(selOut + 1, idx, clean); // 0-based function number
                }
            }
        }

        // --- Persist digitally-set settings (debounced ~3s after last change) ---
        {
            OghamSettings& s = storage.GetSettings();
            // Drone frozen state only needs saving while decoupled; when coupled the
            // engine's drone fields are stale and must not trigger spurious writes.
            bool droneChanged = g_fx.out2Drone &&
                (engine.GetDroneInc()    != s.droneInc ||
                 engine.GetDroneParamA() != s.droneA   ||
                 engine.GetDroneParamB() != s.droneB);
            if (engine.GetFormula1Index() != s.out1Formula ||
                engine.GetFormula2Index() != s.out2Formula ||
                memcmp(&g_fx, &s.fx, sizeof(FxChainConfig)) != 0 ||
                droneChanged) {
                s.out1Formula = engine.GetFormula1Index();
                s.out2Formula = engine.GetFormula2Index();
                s.fx          = g_fx;
                // Capture the drone's frozen rate + A/B (meaningful while decoupled).
                if (g_fx.out2Drone) {
                    s.droneInc = engine.GetDroneInc();
                    s.droneA   = engine.GetDroneParamA();
                    s.droneB   = engine.GetDroneParamB();
                }
                settingsDirty = true;
                lastSettingChangeMs = now;
            }
            if (settingsDirty &&
                (now - lastSettingChangeMs >= SETTINGS_SAVE_DELAY_MS)) {
                storage.Save();        // writes QSPI only if actually changed
                settingsDirty = false;
            }
        }

        // --- Telemetry: snapshot current state for the PC monitor ---
        {
            const uint8_t* sg = tm1637.GetLastSegs();
            g_telemetry.magic   = 0x4F474841;  // "OGHA"
            g_telemetry.counter = g_telemetry.counter + 1;
            g_telemetry.segs    = (uint32_t)sg[0] | ((uint32_t)sg[1] << 8)
                                | ((uint32_t)sg[2] << 16) | ((uint32_t)sg[3] << 24);
            g_telemetry.modeState = (uint32_t)funcMode
                                  | ((uint32_t)selOut << 8)
                                  | ((uint32_t)(pipeline.IsLofiClean() ? 1 : 0) << 16)
                                  | ((uint32_t)(extClockActive ? 1 : 0) << 24)
                                  | ((uint32_t)(clockHeld ? 1 : 0) << 25)
                                  | ((uint32_t)(encTimerRunning ? 1 : 0) << 26);
            g_telemetry.ioState = (uint32_t)(controls.GetGate() ? 1 : 0)
                                | ((uint32_t)(controls.GetClock() ? 1 : 0) << 8);
            g_telemetry.out1Formula = engine.GetFormula1Index();
            g_telemetry.out2Formula = engine.GetFormula2Index();
            g_telemetry.paramA      = engine.GetParamA();
            g_telemetry.paramB      = engine.GetParamB();
            g_telemetry.rate        = engine.GetRate();
            g_telemetry.extClockRate = extClockRate;
            g_telemetry.potA     = controls.GetPot(0);
            g_telemetry.potB     = controls.GetPot(1);
            g_telemetry.potRate  = controls.GetPot(2);
            g_telemetry.potLevel = controls.GetPot(3);
            g_telemetry.combinedA = controls.GetCombinedA();
            g_telemetry.combinedB = controls.GetCombinedB();
            g_telemetry.cvA = controls.GetCvRaw(0);
            g_telemetry.cvB = controls.GetCvRaw(1);
            g_telemetry.cpuPeak   = g_cbWorkPeak;
            g_telemetry.cpuPeriod = g_cbPeriod;
            g_telemetry.syncCount = g_syncCount;
            // fxType repurposed: fxField | parallel<<8 | editing<<16.
            g_telemetry.fxType    = fxField
                                  | ((int)(g_fx.parallel != 0) << 8)
                                  | ((int)fxEditing << 16);
        }
    }
}
