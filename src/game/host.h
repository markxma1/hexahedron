// Portable part of every backend's main(): command line options, the frame loop
// around App (scripted test presses, fixed time step, screenshots). A backend
// only creates the window/context, fills an InputState each frame and calls frame().
#pragma once
#include "app.h"
#include <string>
#include <utility>
#include <vector>

namespace hx {

struct HostOptions {
    LaunchOptions launch;
    std::string shotPath;           // --screenshot PATH FRAME: write a PPM and quit
    int shotFrame = 0;
    int winW = 1600, winH = 900;    // --size WxH (window backends)
    bool mute = false;
    bool fixedStep = false;         // every frame advances 1/60 s (deterministic tests)
    bool gles = false;              // --gles: use OpenGL ES 3.0 (desktop test of the Android path)
    std::vector<std::pair<int, int>> keyScript; // --press FRAME ACTION
    struct Tap { int frame; float x, y; };      // --tap FRAME X Y (pixels)
    std::vector<Tap> tapScript;
    bool forceTouchUi = false;                  // --touch-ui: show the touch buttons (tests)
};

// Returns false if the program should exit right away (e.g. after --help).
bool parseHostArgs(int argc, char** argv, HostOptions& o);

class Host {
public:
    bool init(Platform* p, const HostOptions& o);
    // dt = real seconds since the last frame. Returns false when the game wants to quit.
    bool frame(float dt, InputState in);
    App& app() { return app_; }

private:
    Platform* plat_ = nullptr;
    HostOptions opt_;
    App app_;
    int frameNo_ = 0;
};

} // namespace hx
