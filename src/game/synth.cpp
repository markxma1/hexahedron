#include "synth.h"
#include <cmath>

namespace hx {

namespace {

const float kTau = 6.28318530718f;

struct Style {
    float bpm;
    int root;          // MIDI note of the key
    bool minor;
    int prog[4];       // scale degree per bar
    int padWave, bassWave, arpWave;
    int drums;         // 0 none, 1 soft, 2 groove, 3 four-on-the-floor
    bool sevenths;
    int arpEvery;      // arp note every N 16ths
    float arpOct;      // octave offset of the arp
    int bassMode;      // 0 quarter roots, 1 driving 8ths, 2 offbeat 8ths, 3 funky
};

// One style per theme, same order as the visual themes.
const Style kStyles[7] = {
    {96, 50, true, {0, 5, 2, 6}, 2, 3, 3, 1, false, 2, 1, 0},   // Deep Space
    {112, 45, true, {0, 0, 5, 6}, 2, 2, 1, 2, false, 2, 1, 1},  // Vortex
    {72, 53, false, {0, 3, 5, 4}, 3, 0, 0, 0, false, 2, 2, 0},  // Firefly Night
    {118, 48, false, {1, 4, 0, 5}, 3, 3, 3, 1, true, 2, 1, 0},  // Card Storm
    {100, 55, false, {0, 4, 5, 3}, 2, 1, 3, 2, false, 2, 1, 3}, // Kaleidoscope
    {126, 46, true, {0, 0, 3, 4}, 2, 2, 1, 3, false, 1, 1, 2},  // Quicksilver
    {104, 52, true, {0, 3, 0, 4}, 1, 1, 2, 2, true, 2, 1, 3},   // Gold Rush
};

const int kMinor[7] = {0, 2, 3, 5, 7, 8, 10};
const int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};

float midiHz(float m) { return 440.0f * std::pow(2.0f, (m - 69.0f) / 12.0f); }

int scaleNote(const Style& s, int degree) {
    const int* sc = s.minor ? kMinor : kMajor;
    int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
    int d = degree - oct * 7;
    return s.root + oct * 12 + sc[d];
}

float lpCoefFor(float cutoff, int rate) { return 1.0f - std::exp(-kTau * cutoff / rate); }

} // namespace

void Synth::init(int sampleRate) {
    rate_ = sampleRate;
    voices_.assign(64, Voice{});
    echoL_.assign(rate_, 0);
    echoR_.assign(rate_, 0);
    stepClock_ = 0;
}

float Synth::bpm() const { return kStyles[theme_].bpm; }

float Synth::beatPhase() const {
    float stepLen = 60.0f / bpm() / 4.0f * rate_;
    float stepPos = 1.0f - (float)(stepClock_ / stepLen);
    return ((step_ + 3) % 4 + stepPos) / 4.0f; // step_ already points to the next step
}

void Synth::setTheme(int theme) { pendingTheme_ = theme < 0 ? 0 : theme % 7; }

Synth::Voice* Synth::alloc(bool music) {
    Voice* best = nullptr;
    float bestT = -1;
    for (Voice& v : voices_) {
        if (!v.on) { v = Voice{}; v.music = music; v.noise = (rng_ = rng_ * 1664525u + 1013904223u) | 1; return &v; }
        float rem = v.t / v.dur; // steal the voice closest to its end
        if (rem > bestT) { bestT = rem; best = &v; }
    }
    *best = Voice{};
    best->music = music;
    best->noise = 987654321u;
    return best;
}

void Synth::play(Sfx s, float param) { sfx(s, param); }

void Synth::sfx(Sfx s, float p) {
    auto V = [&](int wave, float f0, float f1, float dur, float vol) {
        Voice* v = alloc(false);
        v->on = true; v->wave = wave; v->freq = f0; v->freqEnd = f1; v->dur = dur; v->vol = vol;
        v->pan = 0.5f;
        return v;
    };
    switch (s) {
    case Sfx::MenuMove: V(3, 880, 880, 0.06f, 0.22f); break;
    case Sfx::MenuSelect: {
        Voice* v = V(3, 660, 660, 0.25f, 0.25f);
        v->arp[0] = 0; v->arp[1] = 7; v->arp[2] = 12; v->arpN = 3; v->arpStep = 0.045f; v->echoSend = 0.3f;
        break;
    }
    case Sfx::MenuBack: V(3, 520, 330, 0.12f, 0.22f); break;
    case Sfx::Move: { Voice* v = V(3, 560, 520, 0.04f, 0.16f); v->pan = 0.5f + p * 0.15f; break; }
    case Sfx::Blocked: { Voice* v = V(1, 130, 110, 0.06f, 0.10f); v->lpCoef = lpCoefFor(900, rate_); break; }
    case Sfx::Land: {
        V(0, 170, 60, 0.14f, 0.45f);
        Voice* n = V(4, 1, 1, 0.03f, 0.12f); n->lpCoef = lpCoefFor(2500, rate_);
        break;
    }
    case Sfx::HardLand: {
        V(0, 200, 45, 0.22f, 0.6f);
        Voice* n = V(4, 1, 1, 0.08f, 0.25f); n->lpCoef = lpCoefFor(1800, rate_);
        break;
    }
    case Sfx::ComboNew: {
        float base = 523.25f * std::pow(2.0f, (p - 2) * 2 / 12.0f);
        for (int k = 0; k < 2; ++k) {
            Voice* v = V(k ? 3 : 0, base * (k ? 2.0f : 1.0f), base * (k ? 2.0f : 1.0f), 0.45f, k ? 0.08f : 0.22f);
            v->arp[0] = 0; v->arp[1] = 4; v->arp[2] = 7; v->arp[3] = 12; v->arpN = 4; v->arpStep = 0.05f;
            v->echoSend = 0.35f;
        }
        break;
    }
    case Sfx::ComboGrow: {
        float f = 659.25f * std::pow(2.0f, std::fmin(p, 15.0f) / 12.0f);
        Voice* v = V(0, f, f, 0.3f, 0.2f); v->echoSend = 0.3f;
        Voice* w = V(3, f * 2, f * 2, 0.15f, 0.05f); w->echoSend = 0.2f;
        break;
    }
    case Sfx::ComboRed: {
        Voice* v = V(1, 440, 330, 0.22f, 0.1f); v->lpCoef = lpCoefFor(2200, rate_);
        break;
    }
    case Sfx::Clear: {
        float f = 1046.5f * std::pow(2.0f, std::fmin(p, 24.0f) / 12.0f);
        Voice* v = V(3, f, f * 1.02f, 0.14f, 0.11f); v->echoSend = 0.4f;
        v->pan = 0.3f + 0.4f * ((int)p % 3) / 2.0f;
        Voice* n = V(4, 1, 1, 0.05f, 0.06f); n->hp = true;
        break;
    }
    case Sfx::Scored: {
        for (int k = 0; k < 3; ++k) {
            Voice* v = V(2, 261.6f * (k + 1) * 0.5f + 0.7f * k, 261.6f * (k + 1) * 0.5f, 0.8f, 0.1f);
            v->arp[0] = 0; v->arp[1] = 4; v->arp[2] = 7; v->arp[3] = 12; v->arp[4] = 16; v->arpN = 5; v->arpStep = 0.06f;
            v->lpCoef = lpCoefFor(3000, rate_); v->echoSend = 0.4f; v->pan = 0.25f + 0.25f * k;
        }
        break;
    }
    case Sfx::WipeKept: { Voice* v = V(0, 300, 1200, 0.3f, 0.14f); v->echoSend = 0.3f; break; }
    case Sfx::MultReset: { Voice* v = V(2, 420, 90, 0.45f, 0.16f); v->lpCoef = lpCoefFor(1600, rate_); break; }
    case Sfx::LevelUp: {
        Voice* v = V(1, 523.25f, 523.25f, 0.8f, 0.14f);
        int a[8] = {0, 4, 7, 12, 7, 12, 16, 19};
        for (int i = 0; i < 8; ++i) v->arp[i] = a[i];
        v->arpN = 8; v->arpStep = 0.075f; v->lpCoef = lpCoefFor(3500, rate_); v->echoSend = 0.4f;
        break;
    }
    case Sfx::Rise: {
        V(0, 75, 48, 0.35f, 0.35f);
        Voice* n = V(4, 1, 1, 0.25f, 0.08f); n->lpCoef = lpCoefFor(400, rate_);
        break;
    }
    case Sfx::GameOver: {
        Voice* v = V(2, 392, 392, 1.6f, 0.2f);
        int a[5] = {12, 7, 3, 0, -5};
        for (int i = 0; i < 5; ++i) v->arp[i] = a[i];
        v->arpN = 5; v->arpStep = 0.22f; v->lpCoef = lpCoefFor(1800, rate_); v->echoSend = 0.5f;
        break;
    }
    case Sfx::Pause: V(3, 600, 600, 0.08f, 0.18f); break;
    case Sfx::Count: break;
    }
}

void Synth::musicStep() {
    if (step_ % 16 == 0 && pendingTheme_ != theme_) theme_ = pendingTheme_;
    const Style& st = kStyles[theme_];
    int bar = (step_ / 16) % 4, s = step_ % 16;
    int deg = st.prog[bar];
    int chord[4] = {scaleNote(st, deg), scaleNote(st, deg + 2), scaleNote(st, deg + 4), scaleNote(st, deg + 6)};
    int nChord = st.sevenths ? 4 : 3;
    float stepSec = 60.0f / st.bpm / 4.0f;
    // Light swing on odd 16ths is done by the caller's clock; here only notes.

    auto M = [&](int wave, float f0, float f1, float dur, float vol) {
        Voice* v = alloc(true);
        v->on = true; v->wave = wave; v->freq = f0; v->freqEnd = f1; v->dur = dur; v->vol = vol;
        return v;
    };

    // Pad: whole-bar chord, slow attack, detuned.
    if (s == 0) {
        for (int i = 0; i < nChord; ++i) {
            float f = midiHz((float)chord[i]);
            Voice* v = M(st.padWave, f, f, stepSec * 16 * 1.05f, 0.04f);
            v->attack = stepSec * 4; v->decayPow = 0.6f; v->detune = 0.004f;
            v->lpCoef = lpCoefFor(1100, rate_);
            v->pan = 0.2f + 0.6f * i / (float)(nChord - 1);
        }
    }

    // Bass.
    bool bassHit = false;
    switch (st.bassMode) {
    case 0: bassHit = (s % 4 == 0); break;
    case 1: bassHit = (s % 2 == 0); break;
    case 2: bassHit = (s % 4 == 2); break;
    case 3: bassHit = (s == 0 || s == 3 || s == 6 || s == 10 || s == 12 || s == 14); break;
    }
    if (bassHit) {
        int note = chord[0] - 12;
        if (st.bassMode == 3 && (s == 10 || s == 14)) note += 12;
        float f = midiHz((float)note);
        Voice* v = M(st.bassWave, f, f, stepSec * (st.bassMode == 0 ? 3.5f : 1.6f), st.bassWave == 0 ? 0.30f : 0.16f);
        v->lpCoef = lpCoefFor(st.bassWave == 0 ? 2000 : 650, rate_);
        v->decayPow = 1.2f;
    }

    // Arpeggio.
    if (s % st.arpEvery == 0) {
        int idx = (s / st.arpEvery + bar) % (nChord * 2);
        int note = chord[idx % nChord] + 12 * (int)st.arpOct + (idx >= nChord ? 12 : 0);
        float f = midiHz((float)note);
        Voice* v = M(st.arpWave, f, f, stepSec * 1.8f, st.arpWave == 1 ? 0.035f : 0.06f);
        v->lpCoef = lpCoefFor(st.arpWave == 1 ? 2400 : 6000, rate_);
        v->echoSend = 0.45f;
        v->pan = 0.5f + 0.35f * std::sin(step_ * 0.7f);
        v->decayPow = 2.0f;
    }

    // Drums.
    if (st.drums > 0) {
        bool kick = st.drums == 3 ? (s % 4 == 0) : (s == 0 || s == 8 || (st.drums == 2 && s == 11));
        bool snare = st.drums >= 2 ? (s == 4 || s == 12) : (s == 12);
        bool hat = st.drums == 3 ? true : (s % 2 == 0);
        if (kick) {
            M(0, 140, 42, 0.22f, st.drums == 1 ? 0.35f : 0.5f)->decayPow = 1.0f;
        }
        if (snare) {
            Voice* n = M(4, 1, 1, 0.16f, st.drums == 1 ? 0.07f : 0.12f);
            n->lpCoef = lpCoefFor(5000, rate_); n->decayPow = 2.0f; n->echoSend = 0.15f;
            M(3, 190, 160, 0.08f, 0.1f);
        }
        if (hat) {
            Voice* h = M(4, 1, 1, (s % 8 == 6 && st.drums >= 2) ? 0.14f : 0.035f, s % 4 == 2 ? 0.05f : 0.03f);
            h->hp = true; h->decayPow = 2.5f; h->pan = 0.62f;
        }
    }
    step_ = (step_ + 1) % 64;
}

float Synth::sampleVoice(Voice& v) {
    float u = v.t / v.dur;
    if (u >= 1) { v.on = false; return 0; }
    float f = v.freq;
    if (v.freqEnd != v.freq) f = v.freq * std::pow(v.freqEnd / v.freq, u);
    if (v.arpN > 0) {
        int i = (int)(v.t / v.arpStep);
        if (i >= v.arpN) i = v.arpN - 1;
        f *= std::pow(2.0f, v.arp[i] / 12.0f);
    }
    float env = (v.t < v.attack ? v.t / v.attack : 1.0f) * std::pow(1.0f - u, v.decayPow);
    if (v.arpN > 0) { // retrigger feel per arp note
        float local = std::fmod(v.t, v.arpStep) / v.arpStep;
        env *= 0.6f + 0.4f * (1.0f - local);
    }
    float o = 0;
    auto osc = [&](float ph) {
        switch (v.wave) {
        case 0: return std::sin(ph * kTau);
        case 1: return ph < 0.5f ? 0.8f : -0.8f;
        case 2: return 2.0f * ph - 1.0f;
        case 3: return 4.0f * std::fabs(ph - 0.5f) - 1.0f;
        default: {
            v.noise ^= v.noise << 13; v.noise ^= v.noise >> 17; v.noise ^= v.noise << 5;
            return (v.noise & 0xFFFF) / 32767.5f - 1.0f;
        }
        }
    };
    o = osc(v.phase);
    if (v.detune > 0 && v.wave != 4) o = 0.5f * (o + osc(v.phase2));
    v.phase += f / rate_;
    v.phase -= std::floor(v.phase);
    v.phase2 += f * (1 + v.detune) / rate_;
    v.phase2 -= std::floor(v.phase2);
    if (v.lpCoef < 1) { v.lp += (o - v.lp) * v.lpCoef; v.lp2 += (v.lp - v.lp2) * v.lpCoef; o = v.lp2; }
    if (v.hp) { v.lp += (o - v.lp) * 0.25f; o = o - v.lp; }
    v.t += 1.0f / rate_;
    return o * env * v.vol;
}

void Synth::render(float* out, int frames) {
    const int echoLen = (int)(60.0f / bpm() / 4.0f * 3.0f * rate_) % (int)echoL_.size();
    for (int i = 0; i < frames; ++i) {
        // 16th-note clock
        stepClock_ -= 1.0;
        if (stepClock_ <= 0) {
            musicStep();
            stepClock_ += 60.0 / kStyles[theme_].bpm / 4.0 * rate_;
        }
        duckCur_ += (duck_ - duckCur_) * 0.0005f;
        float l = 0, r = 0, el = 0, er = 0;
        for (Voice& v : voices_) {
            if (!v.on) continue;
            float s = sampleVoice(v) * (v.music ? musicVol_ * duckCur_ : sfxVol_);
            l += s * (1.0f - v.pan) * 1.4f;
            r += s * v.pan * 1.4f;
            el += s * v.echoSend * (1.0f - v.pan);
            er += s * v.echoSend * v.pan;
        }
        // Ping-pong echo
        int rd = (echoPos_ - (echoLen > 0 ? echoLen : 1) + (int)echoL_.size()) % (int)echoL_.size();
        float dl = echoL_[rd], dr = echoR_[rd];
        echoL_[echoPos_] = el + dr * 0.42f;
        echoR_[echoPos_] = er + dl * 0.42f;
        echoPos_ = (echoPos_ + 1) % (int)echoL_.size();
        l += dl * 0.55f;
        r += dr * 0.55f;
        out[i * 2] = std::tanh(l * 1.8f);
        out[i * 2 + 1] = std::tanh(r * 1.8f);
    }
}

} // namespace hx
