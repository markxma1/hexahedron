#include "host.h"
#include "log.h"
#include <cstdio>
#include <cstdlib>

namespace hx {

bool parseHostArgs(int argc, char** argv, HostOptions& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--autoplay") o.launch.autoplay = true;
        else if (a == "--play") o.launch.skipTitle = true;
        else if (a == "--theme" && i + 1 < argc) o.launch.theme = std::atoi(argv[++i]);
        else if (a == "--mode" && i + 1 < argc) {
            std::string m = argv[++i];
            o.launch.mode = m == "advanced" ? Mode::Advanced : (m == "quick" ? Mode::QuickRun : Mode::Standard);
        } else if (a == "--screenshot" && i + 2 < argc) {
            o.shotPath = argv[++i];
            o.shotFrame = std::atoi(argv[++i]);
            o.fixedStep = true;
        } else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &o.winW, &o.winH);
        else if (a == "--mute") o.mute = true;
        else if (a == "--gles") o.gles = true;
        else if (a == "--touch-ui") o.forceTouchUi = true;
        else if (a == "--tap" && i + 3 < argc) {
            HostOptions::Tap t;
            t.frame = std::atoi(argv[++i]);
            t.x = (float)std::atof(argv[++i]);
            t.y = (float)std::atof(argv[++i]);
            o.tapScript.push_back(t);
        }
        else if (a == "--press" && i + 2 < argc) {
            int f = std::atoi(argv[++i]);
            o.keyScript.push_back({f, std::atoi(argv[++i])});
        } else if (a == "--help") {
            std::printf("hexahedron [--play] [--mode standard|advanced|quick] [--theme 0-6] [--autoplay]\n"
                        "           [--size WxH] [--mute] [--gles] [--screenshot out.ppm FRAME] [--press FRAME ACTION]\n"
                        "           [--tap FRAME X Y] [--touch-ui]\n");
            return false;
        }
    }
    return true;
}

bool Host::init(Platform* p, const HostOptions& o) {
    plat_ = p;
    opt_ = o;
    int w = 0, h = 0;
    p->renderer->viewportSize(w, h);
    logMsg("hexahedron: starting, %dx%d, audio %d Hz", w, h, p->audio ? p->audio->sampleRate() : 0);
    bool ok = app_.init(p, o.launch);
    if (!ok) logMsg("hexahedron: init failed");
    return ok;
}

static void writePPM(const char* path, const std::vector<uint8_t>& rgb, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

bool Host::frame(float dt, InputState in) {
    for (auto& ks : opt_.keyScript)
        if (frameNo_ == ks.first && ks.second >= 0 && ks.second < ActCount) in.held[ks.second] = true;
    for (auto& t : opt_.tapScript)
        if (frameNo_ == t.frame) { in.tap = true; in.tapX = t.x; in.tapY = t.y; }
    if (opt_.forceTouchUi) in.touchUi = true;
    if (opt_.fixedStep) dt = 1.0f / 60.0f;
    app_.frame(dt, in);
    bool keepGoing = !app_.quit();
    if (!opt_.shotPath.empty() && frameNo_ == opt_.shotFrame) {
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        if (plat_->renderer->readPixels(rgb, w, h)) writePPM(opt_.shotPath.c_str(), rgb, w, h);
        else logMsg("screenshot: not supported by this renderer");
        keepGoing = false;
    }
    ++frameNo_;
    return keepGoing;
}

} // namespace hx
