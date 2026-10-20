// Renders music for every theme and all sound effects to WAV + prints levels.
#include "../src/game/synth.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using namespace hx;
static void wav(const std::string& path, const std::vector<float>& s, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    uint32_t n = (uint32_t)s.size(), bytes = n * 2;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (float x : s) { int16_t v = (int16_t)std::lround(std::fmax(-1.f, std::fmin(1.f, x)) * 32767); std::fwrite(&v, 2, 1, f); }
    std::fclose(f);
}
static void stats(const char* name, const std::vector<float>& s) {
    double peak = 0, sum = 0; int nans = 0;
    for (float x : s) { if (std::isnan(x)) ++nans; peak = std::fmax(peak, std::fabs(x)); sum += x * x; }
    std::printf("%-14s peak %.2f  rms %.3f  nan %d\n", name, peak, std::sqrt(sum / s.size()), nans);
}
int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : ".";
    const int rate = 48000;
    for (int t = 0; t < 7; ++t) {
        Synth s; s.init(rate); s.setTheme(t);
        std::vector<float> buf(rate * 12 * 2);
        s.render(buf.data(), rate * 12);
        char name[32]; std::snprintf(name, sizeof name, "music%d", t);
        stats(name, buf); wav(out + "/" + name + ".wav", buf, rate);
    }
    Synth s; s.init(rate); s.setMusicVolume(0);
    std::vector<float> all;
    for (int i = 0; i < (int)Sfx::Count; ++i) {
        s.play((Sfx)i, 4);
        std::vector<float> buf(rate * 2);
        s.render(buf.data(), rate);
        all.insert(all.end(), buf.begin(), buf.end());
    }
    stats("sfx", all); wav(out + "/sfx_all.wav", all, rate);
}
