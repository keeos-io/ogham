#include "ogham_display.h"
#include "daisy_seed.h"

using namespace daisy;

void Display::Init(TM1637* tm) {
    tm_ = tm;
    flashing_ = false;
    flashStartMs_ = 0;
}

void Display::ShowVersion(int major, int minor) {
    if (!tm_) return;
    uint8_t s[4] = {
        0x00,                                                    // leading blank
        (uint8_t)(TM1637::Encode('0' + major % 10) | 0x80),      // major digit + DP
        TM1637::Encode('0' + (minor / 10) % 10),                 // minor tens
        TM1637::Encode('0' + minor % 10),                        // minor units
    };
    tm_->ShowChars(s);
}

void Display::ShowLabeled(uint8_t c0seg, int twoDigit, bool dpClean) {
    if (!tm_ || flashing_) return;
    if (twoDigit < 0) twoDigit = 0;
    if (twoDigit > 99) twoDigit = 99;
    uint8_t segs[4];
    segs[0] = c0seg;
    segs[1] = TM1637::Encode('-');
    segs[2] = TM1637::Encode('0' + (twoDigit / 10) % 10);
    // Far-right DP = Lo-Fi clean-center (Level pot) indicator.
    segs[3] = TM1637::Encode('0' + twoDigit % 10) | (dpClean ? 0x80 : 0x00);
    tm_->ShowChars(segs);
}

void Display::ShowVoice(int outputNum, int functionNum, bool dpClean) {
    ShowLabeled(TM1637::Encode('0' + outputNum), functionNum, dpClean);
}

void Display::ShowVoiceRef(int outputNum, bool dpClean) {
    if (!tm_ || flashing_) return;
    uint8_t segs[4];
    segs[0] = TM1637::Encode('0' + outputNum);
    segs[1] = TM1637::Encode('-');
    segs[2] = TM1637::Encode('A');
    segs[3] = TM1637::Encode('A') | (dpClean ? 0x80 : 0x00);
    tm_->ShowChars(segs);
}

void Display::ShowDelay(int delayCycles, bool dpClean) {
    ShowLabeled(TM1637::Encode('d'), delayCycles, dpClean);
}

void Display::ShowFxEdit(int field, int value, bool parallel, bool dpClean, bool blankValue) {
    if (!tm_ || flashing_) return;
    uint8_t segs[4];

    if (field == 0) {
        // Global FX on/off: "F.on" / "F.off" (first DP after the F).
        segs[0] = TM1637::Encode('F') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value)      { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('n'); segs[3] = 0x00; }
        else                 { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
    } else if (field == 14) {
        // Param-interp grid: "q.oFF" or "q.016".."q.128" (first DP).
        segs[0] = TM1637::Encode('q') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value <= 0) { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
        else {
            segs[1] = TM1637::Encode('0' + (value / 100) % 10);
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    } else if (field == 15) {
        // Out2 decouple/drone: "d.on" / "d.oFF" (first DP; daisy-pcq).
        segs[0] = TM1637::Encode('d') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value)      { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('n'); segs[3] = 0x00; }
        else                 { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
    } else if (field == 16) {
        // CV-out mode (daisy-0pq): "C.En1"/"C.En2" (env followers) / "C.dc1"/"C.dc2" (DC).
        segs[0] = TM1637::Encode('C') | 0x80;
        if (blankValue)         { segs[1] = segs[2] = segs[3] = 0x00; }
        else {
            bool dc = (value >= 2);   // 0,1 = env; 2,3 = DC
            char n  = (value == 1 || value == 3) ? '2' : '1';
            segs[1] = TM1637::Encode(dc ? 'd' : 'E');
            segs[2] = TM1637::Encode(dc ? 'c' : 'n');
            segs[3] = TM1637::Encode(n);
        }
    } else if (field == 17) {
        // CV->Timbre routing (daisy-gtw): "t.oFF" / "t.A" / "t.b" (first DP).
        segs[0] = TM1637::Encode('t') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value == 1) { segs[1] = TM1637::Encode('A'); segs[2] = 0x00; segs[3] = 0x00; }
        else if (value == 2) { segs[1] = TM1637::Encode('b'); segs[2] = 0x00; segs[3] = 0x00; }
        else                 { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
    } else if (field == 18) {
        // Internal LPG on/off: "L.on" / "L.oFF" (first DP; daisy-nmr).
        segs[0] = TM1637::Encode('L') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (value)      { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('n'); segs[3] = 0x00; }
        else                 { segs[1] = TM1637::Encode('o'); segs[2] = TM1637::Encode('F'); segs[3] = TM1637::Encode('F'); }
    } else if (field == 19) {
        // LPG decay: "Ld.NN" — same name-DP-value shape as the stage params.
        segs[0] = TM1637::Encode('L');
        segs[1] = TM1637::Encode('d') | 0x80;
        if (blankValue) { segs[2] = segs[3] = 0x00; }
        else {
            if (value < 0) value = 0;
            if (value > 99) value = 99;
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    } else if (field == 13) {
        // Chain toggle: "c.Ser" / "c.Par" (first DP).
        segs[0] = TM1637::Encode('c') | 0x80;
        if (blankValue)      { segs[1] = segs[2] = segs[3] = 0x00; }
        else if (parallel)   { segs[1] = TM1637::Encode('P'); segs[2] = TM1637::Encode('A'); segs[3] = TM1637::Encode('r'); }
        else                 { segs[1] = TM1637::Encode('5'); segs[2] = TM1637::Encode('E'); segs[3] = TM1637::Encode('r'); }  // '5' = S
    } else {
        // Param fields 1..12: "T x. NN" — digit0 = FX (C/F/P), digit1 = sub
        // (L/t/a/b) + separator DP. stage param field = 1 + stage*4 + sub.
        static const char fxLetter[3] = {'C', 'F', 'P'};
        static const char subGlyph[4] = {'L', 't', 'a', 'b'};
        if (field < 1) field = 1;
        if (field > 12) field = 12;
        int idx = field - 1;
        segs[0] = TM1637::Encode(fxLetter[idx / 4]);
        segs[1] = TM1637::Encode(subGlyph[idx % 4]) | 0x80;
        if (blankValue) { segs[2] = segs[3] = 0x00; }
        else {
            if (value < 0) value = 0;
            if (value > 99) value = 99;
            segs[2] = TM1637::Encode('0' + (value / 10) % 10);
            segs[3] = TM1637::Encode('0' + value % 10);
        }
    }

    // Far-right DP = Lo-Fi clean-center (Level pot) indicator. Applied on EVERY
    // FX-menu screen and in every state (incl. edit-flash), kept live by the
    // 30Hz redraw passing the current clean state.
    if (dpClean) segs[3] |= 0x80;

    tm_->ShowChars(segs);
}

void Display::FlashParam(char prefix, int value) {
    // Only record the pending flash + (re)start the timeout. NO TM1637 write here:
    // the blocking write happens in DrawPendingFlash() at the display tick, so this
    // stays cheap even when called every loop iteration during a fast pot turn.
    pendingPrefix_ = prefix;
    pendingValue_  = value;
    flashRaw_      = false;
    flashing_      = true;
    flashStartMs_  = System::GetNow();
}

void Display::FlashClockRatio(int exp) {
    int mag = 1 << (exp < 0 ? -exp : exp);   // 2^|exp|
    uint8_t s[4] = {0, 0, 0, 0};
    s[3] = TM1637::Encode('0' + mag % 10);
    int leftIdx;                             // cell just left of the number's MSD
    if (mag >= 10) { s[2] = TM1637::Encode('0' + (mag / 10) % 10); leftIdx = 1; }
    else           { leftIdx = 2; }
    if (exp < 0) s[leftIdx] |= 0x01;         // top segment = divide hint
    for (int i = 0; i < 4; i++) pendingSegs_[i] = s[i];
    flashRaw_     = true;
    flashing_     = true;
    flashStartMs_ = System::GetNow();
}

void Display::UpdateFlashValue(char prefix, int value) {
    // Keep the live value current (incl. CV) during an active flash, but leave
    // flashStartMs_ alone so the timeout still expires after the last knob turn.
    if (!flashing_ || prefix != pendingPrefix_) return;
    pendingValue_ = value;
}

void Display::DrawPendingFlash(bool dpClean) {
    if (!tm_ || !flashing_) return;
    if (flashRaw_) {
        uint8_t s[4] = { pendingSegs_[0], pendingSegs_[1], pendingSegs_[2], pendingSegs_[3] };
        if (dpClean) s[3] |= 0x80;   // far-right DP = Lo-Fi clean-center indicator
        tm_->ShowChars(s);
    } else {
        tm_->ShowPrefixNumber(pendingPrefix_, pendingValue_, dpClean);
    }
}

void Display::Update() {
    if (!tm_) return;

    // Time out the param flash; the main loop redraws the normal display.
    if (flashing_) {
        uint32_t now = System::GetNow();
        if (now - flashStartMs_ >= FLASH_DURATION_MS) {
            flashing_ = false;
        }
    }
}
