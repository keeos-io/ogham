#include "ogham_controls.h"
#include <cmath>

// Encoder direction (daisy-0wf). Two encoder batches exist with opposite A/B
// phase. The DEFAULT is now the reversed/swapped-A/B part, because that is what
// modules 2-5 (and everything built since) actually use -- having the default
// match the minority part meant the wrong build kept getting flashed. Module 1
// has the original encoder: build it with -DENC_REVERSED=0 (see
// Makefile.encorig, output ogham_bytebeat_encorig). Reverting a unit either way
// is just a matter of flashing the other build.
#ifndef ENC_REVERSED
#define ENC_REVERSED 1
#endif

void Controls::Init(daisy::DaisySeed* seed,
                    daisy::Encoder* encoder,
                    daisy::GateIn* gateIn,
                    daisy::GateIn* clockIn) {
    seed_ = seed;
    encoder_ = encoder;
    gateIn_ = gateIn;
    clockIn_ = clockIn;

    // Mode toggle input (Clock <-> V/oct): SPDT common -> GND, internal pull-up.
    modeGpio_.Init(seed_->GetPin(ogham::MODE_SW),
                   daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    voctSmoothed_ = 0.0f;
    voctMode_ = false;
    modeDebounce_ = 0;

    for (int i = 0; i < 4; i++) {
        potValues_[i] = 0.0f;
        potSmoothed_[i] = 0.0f;
    }
    cvSmoothed_[0] = CV_ZERO_OFFSET_A;
    cvSmoothed_[1] = CV_ZERO_OFFSET_B;
    combinedA_ = 0.5f;
    combinedB_ = 0.5f;
    encoderInc_ = 0;
    encoderPressed_ = false;
    encoderRising_ = false;
    lastEncoderPressed_ = false;

    // Direct A/B reads for the ISR-sampled quadrature decoder (SampleEncoder).
    // libDaisy's Encoder also owns these pins for the switch; a second INPUT+PULLUP
    // config is idempotent and reads are independent.
    encA_.Init(seed_->GetPin(ogham::ENC_A), daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    encB_.Init(seed_->GetPin(ogham::ENC_B), daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    encPrevState_ = (uint8_t)((encA_.Read() ? 2 : 0) | (encB_.Read() ? 1 : 0));
    encDelta_    = 0;
    encSubAccum_ = 0;

    gate_ = false;
    gateRising_ = false;
    lastGate_ = false;
    clock_ = false;
    clockRising_ = false;
    lastClock_ = false;
}

void Controls::PrimeSmoothing() {
    if (!seed_) return;
    for (int i = 0; i < 4; i++) potSmoothed_[i] = seed_->adc.GetFloat(i);
    cvSmoothed_[0] = seed_->adc.GetFloat(4);
    cvSmoothed_[1] = seed_->adc.GetFloat(5);
    voctSmoothed_ = seed_->adc.GetFloat(6);
    Process();  // recompute potValues_ / combinedA_ / combinedB_ from the snapped state
}

void Controls::SampleEncoder() {
    // Full-quadrature state machine. QDEC[(prev<<2)|cur] gives the sub-step (+1/-1
    // for a valid single-state transition, 0 for none/invalid). 4 sub-steps in one
    // direction = one detent. Bounce nets to ~0 and never reaches +-4, so it's
    // rejected. Runs at 1kHz in the audio ISR -> immune to the main-loop display stall.
    static const int8_t QDEC[16] = {
         0, +1, -1,  0,
        -1,  0,  0, +1,
        +1,  0,  0, -1,
         0, -1, +1,  0,
    };
    uint8_t cur = (uint8_t)((encA_.Read() ? 2 : 0) | (encB_.Read() ? 1 : 0));
    encSubAccum_ += QDEC[(encPrevState_ << 2) | cur];
    encPrevState_ = cur;
    if (encSubAccum_ >= 4)  { encDelta_++; encSubAccum_ -= 4; }
    if (encSubAccum_ <= -4) { encDelta_--; encSubAccum_ += 4; }
}

void Controls::Process() {
    if (!seed_ || !encoder_) return;

    // Read and smooth the 4 pot ADC channels (ADC0-ADC3)
    for (int i = 0; i < 4; i++) {
        float raw = seed_->adc.GetFloat(i);
        float coeff = (i < 2) ? SMOOTH_COEFF_AB : SMOOTH_COEFF;
        potSmoothed_[i] += coeff * (raw - potSmoothed_[i]);
        potValues_[i] = potSmoothed_[i];
    }

    // Read and smooth MCP6004 outputs (ADC4-ADC5)
    // These already contain combined pot+CV+offset from hardware summing amp.
    // MCP6004 output: V_out = -V_cv - V_pot + 2.5V (clamped 0-3.3V)
    // High ADC = negative CV / low pot, Low ADC = positive CV / high pot
    for (int i = 0; i < 2; i++) {
        float raw = seed_->adc.GetFloat(4 + i);
        cvSmoothed_[i] += SMOOTH_COEFF_CV * (raw - cvSmoothed_[i]);
    }

    // V/oct input (ADC6): direct 0-3.3V tap (divider+clamp+U3D buffer), smoothed.
    voctSmoothed_ += SMOOTH_COEFF_CV * (seed_->adc.GetFloat(6) - voctSmoothed_);

    // Mode toggle (debounced): matches panel labelling — HIGH (switch floats to the
    // pull-up) = V/oct, LOW (switch -> GND) = Clock. (Polarity confirmed on hardware.)
    bool rawVoct = modeGpio_.Read();
    if (rawVoct != voctMode_) {
        if (++modeDebounce_ > 5) { voctMode_ = rawVoct; modeDebounce_ = 0; }
    } else {
        modeDebounce_ = 0;
    }

    // Invert and normalize MCP6004 output to 0-1 parameter range.
    // At 0V CV + 0V pot (pot CCW): ADC = CV_ZERO_OFFSET_x → combined = 0.0
    // At 0V CV + full pot (pot CW): ADC ≈ 0.0 → combined = 1.0
    // Per-channel offsets measured on this board (monitor, 2026-06-25).
    float valA = (CV_ZERO_OFFSET_A - cvSmoothed_[0]) / CV_ZERO_OFFSET_A;
    if (valA < 0.0f) valA = 0.0f;
    if (valA > 1.0f) valA = 1.0f;
    combinedA_ = valA;

    float valB = (CV_ZERO_OFFSET_B - cvSmoothed_[1]) / CV_ZERO_OFFSET_B;
    if (valB < 0.0f) valB = 0.0f;
    if (valB > 1.0f) valB = 1.0f;
    combinedB_ = valB;

    // Encoder switch (still main-loop polled; a click spans many iterations).
    encoder_->Debounce();
    // Rotation comes from the ISR-sampled decoder (SampleEncoder, 1kHz in the audio
    // callback), NOT encoder_->Increment() -- so detents aren't dropped while the
    // main loop is stalled in a display write. Drain the accumulator atomically.
    int32_t d;
    __disable_irq();
    d = encDelta_;
    encDelta_ = 0;
    __enable_irq();
#if ENC_REVERSED
    encoderInc_ = (int)d;    // swapped-A/B encoder (default): CW increases
#else
    encoderInc_ = -(int)d;   // original encoder: negate so CW increases (formula/delay)
#endif
    encoderPressed_ = encoder_->Pressed();
    encoderRising_ = encoderPressed_ && !lastEncoderPressed_;
    lastEncoderPressed_ = encoderPressed_;

    // Gate input (reset)
    gate_ = gateIn_->State();
    gateRising_ = gate_ && !lastGate_;
    lastGate_ = gate_;

    // Clock input
    clock_ = clockIn_->State();
    clockRising_ = clock_ && !lastClock_;
    lastClock_ = clock_;
}

int32_t Controls::MapKnobToRange(float knob, int32_t min, int32_t max) {
    if (min >= max) return min;
    float val = (float)min + knob * (float)(max - min);
    int32_t result = (int32_t)(val + 0.5f);
    if (result < min) result = min;
    if (result > max) result = max;
    return result;
}

float Controls::MapKnobToRate(float knob) {
    // Exponential mapping: 0.0 -> 0.25x, 0.5 -> 1.0x, 1.0 -> 4.0x
    return 0.25f * powf(2.0f, 4.0f * knob);
}
