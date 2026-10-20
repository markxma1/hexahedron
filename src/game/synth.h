// Software synthesizer: sound effects + procedural music per theme.
// Produces interleaved stereo float PCM; the platform only plays it back.
#pragma once
#include <cstdint>
#include <vector>

namespace hx {

enum class Sfx {
    MenuMove, MenuSelect, MenuBack, Move, Blocked, Land, HardLand, ComboNew, ComboGrow,
    ComboRed, Clear, Scored, WipeKept, MultReset, LevelUp, Rise, GameOver, Pause, Count
};

class Synth {
public:
    void init(int sampleRate);
    void play(Sfx s, float param = 0);
    void setTheme(int theme);       // switches the music style at the next bar
    void render(float* stereo, int frames);
    float bpm() const;
    float beatPhase() const;        // 0..1 within the current beat (for visual pulses)
    void setMusicVolume(float v) { musicVol_ = v; }
    void setSfxVolume(float v) { sfxVol_ = v; }
    void setMusicDuck(float d) { duck_ = d; } // 0..1 (pause menu etc.)

    struct Voice {
        bool on = false;
        int wave = 0;               // 0 sine, 1 square, 2 saw, 3 tri, 4 noise
        float freq = 440, freqEnd = 440;
        float t = 0, dur = 0.1f, attack = 0.005f, decayPow = 1.5f;
        float vol = 0.3f, pan = 0.5f;
        float phase = 0, phase2 = 0, detune = 0;
        float lpCoef = 1, lp = 0, lp2 = 0; bool hp = false;
        int arp[8] = {0}; int arpN = 0; float arpStep = 0;
        float echoSend = 0;
        uint32_t noise = 22222;
        bool music = false;
    };

private:
    Voice* alloc(bool music);
    void sfx(Sfx s, float param);
    void musicStep();
    float sampleVoice(Voice& v);

    int rate_ = 48000;
    std::vector<Voice> voices_;
    float sfxVol_ = 0.8f, musicVol_ = 0.55f, duck_ = 1.0f, duckCur_ = 1.0f;
    int theme_ = 0, pendingTheme_ = 0;
    double stepClock_ = 0;          // samples until next 16th step
    int step_ = 0;                  // 0..63 (4 bars of 16ths)
    std::vector<float> echoL_, echoR_;
    int echoPos_ = 0;
    uint32_t rng_ = 1234567;
};

} // namespace hx
