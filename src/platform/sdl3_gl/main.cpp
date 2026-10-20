// SDL3 + OpenGL backend: window, input, audio, files. PC (Linux/Windows), later Android.
#include "../../game/app.h"
#include "gl_renderer.h"
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace hx;

namespace {

class SDLAudio : public Audio {
public:
    bool init() {
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = 48000;
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_) { std::fprintf(stderr, "audio: %s\n", SDL_GetError()); return false; }
        SDL_ResumeAudioStreamDevice(stream_);
        return true;
    }
    int sampleRate() override { return 48000; }
    int queuedFrames() override { return stream_ ? SDL_GetAudioStreamQueued(stream_) / 8 : 1 << 20; }
    void push(const float* stereo, int frames) override {
        if (stream_) SDL_PutAudioStreamData(stream_, stereo, frames * 8);
    }
    bool muted = false;

private:
    SDL_AudioStream* stream_ = nullptr;
};

class NullAudio : public Audio {
public:
    int sampleRate() override { return 48000; }
    int queuedFrames() override { return 0; }
    void push(const float*, int) override {}
};

class SDLFiles : public Files {
public:
    void init() {
        const char* base = SDL_GetBasePath();
        std::string b = base ? base : "./";
        candidates_ = {b + "assets/", b + "../assets/", "assets/"};
#ifdef HX_SOURCE_ASSETS
        candidates_.push_back(HX_SOURCE_ASSETS "/");
#endif
        const char* over = SDL_getenv("HX_SAVE_DIR"); // tests: keep the real save dir clean
        if (over && *over) {
            pref_ = std::string(over) + "/";
            SDL_CreateDirectory(over);
        } else {
            char* pref = SDL_GetPrefPath("Xma1", "Hexahedron");
            pref_ = pref ? pref : "./";
            SDL_free(pref);
            // Saves from before the rename are still read (never written).
            const std::string tail = "Xma1/Hexahedron/";
            if (pref_.size() > tail.size() && pref_.compare(pref_.size() - tail.size(), tail.size(), tail) == 0)
                legacy_ = pref_.substr(0, pref_.size() - tail.size()) + "Hexaxis/HexaxisRemake/";
        }
    }
    bool readAsset(const std::string& name, std::vector<uint8_t>& out) override {
        for (const std::string& dir : candidates_) {
            size_t size = 0;
            void* data = SDL_LoadFile((dir + name).c_str(), &size);
            if (!data) continue;
            out.assign((uint8_t*)data, (uint8_t*)data + size);
            SDL_free(data);
            return true;
        }
        return false;
    }
    bool loadText(const std::string& name, std::string& out) override {
        size_t size = 0;
        void* data = SDL_LoadFile((pref_ + name).c_str(), &size);
        if (!data && !legacy_.empty()) data = SDL_LoadFile((legacy_ + name).c_str(), &size);
        if (!data) return false;
        out.assign((char*)data, size);
        SDL_free(data);
        return true;
    }
    bool saveText(const std::string& name, const std::string& data) override {
        return SDL_SaveFile((pref_ + name).c_str(), data.data(), data.size());
    }

private:
    std::vector<std::string> candidates_;
    std::string pref_, legacy_;
};

void writePPM(const char* path, const std::vector<uint8_t>& rgb, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

} // namespace

int main(int argc, char** argv) {
    LaunchOptions opt;
    const char* shotPath = nullptr;
    int shotFrame = 0, winW = 1600, winH = 900;
    bool mute = false, fixedStep = false;
    std::vector<std::pair<int, int>> keyScript; // (frame, action) presses for testing
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--autoplay") opt.autoplay = true;
        else if (a == "--play") opt.skipTitle = true;
        else if (a == "--theme" && i + 1 < argc) opt.theme = std::atoi(argv[++i]);
        else if (a == "--mode" && i + 1 < argc) {
            std::string m = argv[++i];
            opt.mode = m == "advanced" ? Mode::Advanced : (m == "quick" ? Mode::QuickRun : Mode::Standard);
        } else if (a == "--screenshot" && i + 2 < argc) { shotPath = argv[++i]; shotFrame = std::atoi(argv[++i]); fixedStep = true; }
        else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &winW, &winH);
        else if (a == "--mute") mute = true;
        else if (a == "--press" && i + 2 < argc) { int f = std::atoi(argv[++i]); keyScript.push_back({f, std::atoi(argv[++i])}); }
        else if (a == "--help") {
            std::printf("hexahedron [--play] [--mode standard|advanced|quick] [--theme 0-6] [--autoplay]\n"
                        "        [--size WxH] [--mute] [--screenshot out.ppm FRAME] [--press FRAME ACTION]\n");
            return 0;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    SDL_Window* win = SDL_CreateWindow("Hexahedron", winW, winH,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { std::fprintf(stderr, "GL context: %s\n", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(win, ctx);
    if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);
    if (!hxLoadGL()) return 1;

    std::fprintf(stderr, "GL: %s / %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    GLRenderer renderer;
    if (!renderer.init(4)) return 1;
    SDLAudio sdlAudio;
    NullAudio nullAudio;
    Audio* audio = &nullAudio;
    if (!mute && !shotPath && sdlAudio.init()) audio = &sdlAudio;
    SDLFiles files;
    files.init();

    Platform plat;
    plat.renderer = &renderer;
    plat.audio = audio;
    plat.files = &files;

    App app;
    int pw, ph;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    renderer.setViewport(pw, ph);
    if (!app.init(&plat, opt)) return 1;

    std::vector<SDL_Gamepad*> pads;
    Uint64 last = SDL_GetTicksNS();
    int frameNo = 0;
    bool running = true;
    float mouseDX = 0, mouseDY = 0, wheel = 0;
    bool lookReset = false;
    while (running) {
        mouseDX = mouseDY = wheel = 0;
        lookReset = false;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)))
                    plat.requestFullscreenToggle = true;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                // drag with left or right button turns the camera a little
                if (ev.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) { mouseDX += ev.motion.xrel; mouseDY += ev.motion.yrel; }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_MIDDLE || ev.button.clicks >= 2) lookReset = true;
                break;
            case SDL_EVENT_MOUSE_WHEEL: wheel += ev.wheel.y; break;
            case SDL_EVENT_GAMEPAD_ADDED: {
                SDL_Gamepad* g = SDL_OpenGamepad(ev.gdevice.which);
                if (g) pads.push_back(g);
                break;
            }
            case SDL_EVENT_GAMEPAD_REMOVED:
                for (size_t i = 0; i < pads.size(); ++i)
                    if (SDL_GetGamepadID(pads[i]) == ev.gdevice.which) { SDL_CloseGamepad(pads[i]); pads.erase(pads.begin() + i); break; }
                break;
            default: break;
            }
        }
        if (plat.requestFullscreenToggle) {
            plat.requestFullscreenToggle = false;
            plat.fullscreen = !plat.fullscreen;
            SDL_SetWindowFullscreen(win, plat.fullscreen);
        }

        InputState in;
        const bool* k = SDL_GetKeyboardState(nullptr);
        in.held[ActLeft] = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A];
        in.held[ActRight] = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
        in.held[ActUp] = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W];
        in.held[ActDown] = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
        in.held[ActDrop] = k[SDL_SCANCODE_SPACE];
        in.held[ActConfirm] = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
        in.held[ActBack] = k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE];
        in.held[ActPause] = k[SDL_SCANCODE_P];
        in.held[ActHelp] = k[SDL_SCANCODE_F1] || k[SDL_SCANCODE_H];
        in.held[ActZoomIn] = k[SDL_SCANCODE_Q];
        in.held[ActZoomOut] = k[SDL_SCANCODE_E];
        in.held[ActPrevTheme] = k[SDL_SCANCODE_LEFTBRACKET];
        in.held[ActNextTheme] = k[SDL_SCANCODE_RIGHTBRACKET];
        {
            int ww = 1, wh = 1;
            SDL_GetWindowSize(win, &ww, &wh);
            in.lookDX = mouseDX / (float)(wh > 0 ? wh : 1);
            in.lookDY = mouseDY / (float)(wh > 0 ? wh : 1);
        }
        in.zoomSteps = wheel;
        in.lookReset = lookReset || k[SDL_SCANCODE_X];
        in.lookRateX = (k[SDL_SCANCODE_C] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_Z] ? 1.0f : 0.0f);
        for (SDL_Gamepad* g : pads) {
            auto B = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(g, b); };
            float lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
            float ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
            in.held[ActLeft] |= B(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -0.5f;
            in.held[ActRight] |= B(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 0.5f;
            in.held[ActUp] |= B(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -0.6f;
            in.held[ActDown] |= B(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 0.5f;
            in.held[ActDrop] |= B(SDL_GAMEPAD_BUTTON_SOUTH);
            in.held[ActConfirm] |= B(SDL_GAMEPAD_BUTTON_SOUTH);
            in.held[ActBack] |= B(SDL_GAMEPAD_BUTTON_EAST);
            in.held[ActPause] |= B(SDL_GAMEPAD_BUTTON_START);
            in.held[ActHelp] |= B(SDL_GAMEPAD_BUTTON_BACK);
            in.held[ActZoomIn] |= B(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            in.held[ActZoomOut] |= B(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
            float rx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
            float ry = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
            if (std::fabs(rx) > 0.2f) in.lookRateX += rx;
            if (std::fabs(ry) > 0.2f) in.lookRateY += ry;
            in.lookReset |= B(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        }
        for (auto& ks : keyScript)
            if (frameNo == ks.first && ks.second >= 0 && ks.second < ActCount) in.held[ks.second] = true;

        Uint64 now = SDL_GetTicksNS();
        float dt = fixedStep ? 1.0f / 60.0f : (now - last) / 1e9f;
        last = now;

        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        renderer.setViewport(pw, ph);
        app.frame(dt, in);
        if (app.quit()) running = false;

        if (shotPath && frameNo == shotFrame) {
            std::vector<uint8_t> rgb;
            renderer.readPixels(rgb);
            writePPM(shotPath, rgb, pw, ph);
            running = false;
        }
        SDL_GL_SwapWindow(win);
        ++frameNo;
    }
    for (SDL_Gamepad* g : pads) SDL_CloseGamepad(g);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
