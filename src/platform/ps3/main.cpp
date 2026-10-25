// PS3 backend (PSL1GHT): RSX renderer, ioPad input, audio port, files on the HDD.
// Everything else is the shared game code (src/core, src/game) - unchanged.
#include "../../game/host.h"
#include "rsx_renderer.h"
#include <audio/audio.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <io/pad.h>
#include <string>
#include <sys/stat.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>
#include <vector>

// fonts linked into the program (bin2o), so no asset files are needed
#include "Exo2_ttf.h"
#include "Orbitron_ttf.h"

using namespace hx;

namespace {

bool gQuit = false;

void onSysutil(u64 status, u64 param, void* user) {
    (void)param; (void)user;
    if (status == SYSUTIL_EXIT_GAME) gQuit = true; // PS button -> Quit Game
}

class PS3Audio : public Audio {
public:
    bool init() {
        if (audioInit() != 0) return false;
        audioPortParam p;
        std::memset(&p, 0, sizeof p);
        p.numChannels = AUDIO_PORT_2CH;
        p.numBlocks = AUDIO_BLOCK_16;
        p.attrib = 0;
        p.level = 1.0f;
        if (audioPortOpen(&p, &port_) != 0) return false;
        audioGetPortConfig(port_, &cfg_);
        audioPortStart(port_);
        readIndex_ = (volatile u64*)(u64)cfg_.readIndex;
        data_ = (float*)(u64)cfg_.audioDataStart;
        blocks_ = (int)cfg_.numBlocks;
        write_ = ((int)*readIndex_ + 2) % blocks_; // start a little ahead of the reader
        ok_ = true;
        return true;
    }
    int sampleRate() override { return 48000; }
    // Frames written ahead of the block the hardware is playing.
    int queuedFrames() override {
        if (!ok_) return 1 << 20;
        int rd = (int)*readIndex_;
        int ahead = (write_ - rd + blocks_) % blocks_;
        return ahead * AUDIO_BLOCK_SAMPLES + fill_;
    }
    void push(const float* stereo, int frames) override {
        if (!ok_) return;
        while (frames > 0) {
            int rd = (int)*readIndex_;
            if ((write_ + 1) % blocks_ == rd) return;   // ring full: drop (the game keeps ~70 ms queued)
            float* blk = data_ + (size_t)write_ * 2 * AUDIO_BLOCK_SAMPLES;
            int n = std::min(frames, AUDIO_BLOCK_SAMPLES - fill_);
            std::memcpy(blk + fill_ * 2, stereo, n * 2 * sizeof(float));
            fill_ += n; stereo += n * 2; frames -= n;
            if (fill_ == AUDIO_BLOCK_SAMPLES) { fill_ = 0; write_ = (write_ + 1) % blocks_; }
        }
    }
    void shutdown() {
        if (!ok_) return;
        audioPortStop(port_);
        audioPortClose(port_);
        audioQuit();
    }

private:
    u32 port_ = 0;
    audioPortConfig cfg_;
    volatile u64* readIndex_ = nullptr;
    float* data_ = nullptr;
    int blocks_ = 16, write_ = 0, fill_ = 0;
    bool ok_ = false;
};

class NullAudio : public Audio {
public:
    int sampleRate() override { return 48000; }
    int queuedFrames() override { return 0; }
    void push(const float*, int) override {}
};

class PS3Files : public Files {
public:
    explicit PS3Files(const std::string& dir) : dir_(dir) { mkdir(dir_.c_str(), 0777); }
    bool readAsset(const std::string& name, std::vector<uint8_t>& out) override {
        if (name == "fonts/Orbitron.ttf") { out.assign(Orbitron_ttf, Orbitron_ttf + Orbitron_ttf_size); return true; }
        if (name == "fonts/Exo2.ttf") { out.assign(Exo2_ttf, Exo2_ttf + Exo2_ttf_size); return true; }
        return readFile(dir_ + "assets/" + name, out);
    }
    bool loadText(const std::string& name, std::string& out) override {
        std::vector<uint8_t> d;
        if (!readFile(dir_ + name, d)) return false;
        out.assign(d.begin(), d.end());
        return true;
    }
    bool saveText(const std::string& name, const std::string& data) override {
        FILE* f = std::fopen((dir_ + name).c_str(), "wb");
        if (!f) return false;
        bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
        std::fclose(f);
        return ok;
    }

private:
    static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        out.resize(n > 0 ? (size_t)n : 0);
        bool ok = n <= 0 || std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
        std::fclose(f);
        return ok;
    }
    std::string dir_;
};

// Folder of the running EBOOT (USRDIR when installed as a package), with trailing '/'.
std::string programDir(int argc, char** argv) {
    if (argc > 0 && argv[0] && std::strchr(argv[0], '/')) {
        std::string p = argv[0];
        return p.substr(0, p.rfind('/') + 1);
    }
    return "/dev_hdd0/game/HEXA00001/USRDIR/";
}

// Optional test options (same flags as on PC) from args.txt next to the program,
// because the PS3 cannot pass a command line.
std::vector<std::string> readArgsFile(Files& files) {
    std::vector<std::string> args;
    std::string text;
    if (!files.loadText("args.txt", text)) return args;
    std::string cur;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { if (!cur.empty()) args.push_back(cur), cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) args.push_back(cur);
    return args;
}

float axis(unsigned v) { // 0..255, 128 = center
    float a = ((int)v - 128) / 127.0f;
    return std::fabs(a) < 0.25f ? 0.0f : a;
}

} // namespace

int main(int argc, char** argv) {
    std::string dir = programDir(argc, argv);
    PS3Files files(dir);

    HostOptions opt;
    std::vector<std::string> extra = readArgsFile(files);
    std::vector<char*> av;
    av.push_back(argv && argc > 0 ? argv[0] : (char*)"hexahedron");
    for (std::string& s : extra) av.push_back(&s[0]);
    parseHostArgs((int)av.size(), av.data(), opt);
    if (!opt.shotPath.empty() && opt.shotPath[0] != '/') opt.shotPath = dir + opt.shotPath;

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, onSysutil, nullptr);
    ioPadInit(7);

    RSXRenderer renderer;
    if (!renderer.init()) return 1;
    PS3Audio ps3Audio;
    NullAudio nullAudio;
    Audio* audio = &nullAudio;
    if (!opt.mute && ps3Audio.init()) audio = &ps3Audio;

    Platform plat;
    plat.renderer = &renderer;
    plat.audio = audio;
    plat.files = &files;
    plat.fullscreen = true;
    plat.hasWindow = false;
    Host host;
    if (!host.init(&plat, opt)) return 1;

    u64 last = sysGetSystemTime(); // microseconds
    while (!gQuit) {
        sysUtilCheckCallback();
        InputState in;
        padInfo info;
        ioPadGetInfo(&info);
        for (int i = 0; i < MAX_PADS; ++i) {
            if (!info.status[i]) continue;
            padData p;
            if (ioPadGetData(i, &p) != 0 || p.len == 0) continue;
            float lx = axis(p.ANA_L_H), ly = axis(p.ANA_L_V), rx = axis(p.ANA_R_H), ry = axis(p.ANA_R_V);
            in.held[ActLeft] |= p.BTN_LEFT || lx < -0.5f;
            in.held[ActRight] |= p.BTN_RIGHT || lx > 0.5f;
            in.held[ActUp] |= p.BTN_UP || ly < -0.6f;
            in.held[ActDown] |= p.BTN_DOWN || ly > 0.5f;
            in.held[ActDrop] |= p.BTN_CROSS;
            in.held[ActConfirm] |= p.BTN_CROSS;
            in.held[ActBack] |= p.BTN_CIRCLE;
            in.held[ActPause] |= p.BTN_START;
            in.held[ActHelp] |= p.BTN_SELECT;
            in.held[ActZoomIn] |= p.BTN_L1;
            in.held[ActZoomOut] |= p.BTN_R1;
            in.held[ActPrevTheme] |= p.BTN_L2;
            in.held[ActNextTheme] |= p.BTN_R2;
            in.lookRateX += rx;
            in.lookRateY += ry;
            in.lookReset |= p.BTN_R3;
        }
        u64 now = sysGetSystemTime();
        float dt = (now - last) / 1e6f;
        last = now;
        if (!host.frame(dt, in)) break;
    }
    ps3Audio.shutdown();
    ioPadEnd();
    return 0;
}
