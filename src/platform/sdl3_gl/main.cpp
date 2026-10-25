// SDL3 + OpenGL (ES) backend: window, input, audio, files.
// The same file builds for PC (Linux/Windows, GL 3.3) and Android (GLES 3.0, touch).
#include "../../game/host.h"
#include "../../game/log.h"
#include "../../game/touch.h"
#include "gl_renderer.h"
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
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
        if (!stream_) { logMsg("audio: %s", SDL_GetError()); return false; }
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
#ifdef SDL_PLATFORM_ANDROID
        candidates_ = {""}; // APK assets: SDL_LoadFile reads relative paths from the APK
#endif
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

} // namespace

#ifdef SDL_PLATFORM_ANDROID
#include <android/log.h>
static void androidLog(const char* line) { __android_log_write(ANDROID_LOG_INFO, "Hexahedron", line); }
#endif

int main(int argc, char** argv) {
#ifdef SDL_PLATFORM_ANDROID
    setLogSink(androidLog);
#endif
    HostOptions opt;
    if (!parseHostArgs(argc, argv, opt)) return 0;
#ifdef SDL_PLATFORM_ANDROID
    opt.gles = true;
#endif

    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); // touches come as finger events only
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        logMsg("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (opt.gles) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef SDL_PLATFORM_ANDROID
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    SDL_Window* win = SDL_CreateWindow("Hexahedron", opt.winW, opt.winH, flags);
    if (!win) { logMsg("window: %s", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { logMsg("GL context: %s", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(win, ctx);
    if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);
    if (!hxLoadGL()) return 1;

    logMsg("GL: %s / %s", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    GLRenderer renderer;
    if (!renderer.init(4, opt.gles)) return 1;
    SDLAudio sdlAudio;
    NullAudio nullAudio;
    Audio* audio = &nullAudio;
    if (!opt.mute && opt.shotPath.empty() && sdlAudio.init()) audio = &sdlAudio;
    SDLFiles files;
    files.init();

    Platform plat;
    plat.renderer = &renderer;
    plat.audio = audio;
    plat.files = &files;
#ifdef SDL_PLATFORM_ANDROID
    plat.fullscreen = true;
    plat.hasWindow = false;
#endif

    int pw, ph;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    renderer.setViewport(pw, ph);
    Host host;
    if (!host.init(&plat, opt)) return 1;

    TouchGestures touch;
    std::vector<SDL_Gamepad*> pads;
    Uint64 last = SDL_GetTicksNS();
    bool running = true;
    // mouse: drag = look around, click without drag = tap (menus)
    float mouseDX = 0, mouseDY = 0, wheel = 0, pressX = 0, pressY = 0, dragDist = 0;
    bool lookReset = false, mouseTap = false, backKey = false;
    float tapX = 0, tapY = 0;
    while (running) {
        mouseDX = mouseDY = wheel = 0;
        lookReset = mouseTap = backKey = false;
        double now_s = SDL_GetTicksNS() / 1e9;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        int ww = 1, wh = 1;
        SDL_GetWindowSize(win, &ww, &wh);
        float pxScale = ww > 0 ? (float)pw / ww : 1.0f; // window coords -> pixels
        touch.setScreen(pw, ph);
        {
            SDL_Rect safe;
            if (SDL_GetWindowSafeArea(win, &safe)) {
                plat.safeLeft = (int)(safe.x * pxScale);
                plat.safeTop = (int)(safe.y * pxScale);
                plat.safeRight = (int)((ww - safe.x - safe.w) * pxScale);
                plat.safeBottom = (int)((wh - safe.y - safe.h) * pxScale);
            }
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)))
                    plat.requestFullscreenToggle = true;
                if (ev.key.key == SDLK_AC_BACK) backKey = true; // Android back button
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (ev.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) {
                    mouseDX += ev.motion.xrel; mouseDY += ev.motion.yrel;
                    dragDist += std::fabs(ev.motion.xrel) + std::fabs(ev.motion.yrel);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_MIDDLE || ev.button.clicks >= 2) lookReset = true;
                if (ev.button.button == SDL_BUTTON_LEFT) { pressX = ev.button.x; pressY = ev.button.y; dragDist = 0; }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT && dragDist < 6) {
                    mouseTap = true; tapX = ev.button.x * pxScale; tapY = ev.button.y * pxScale;
                }
                (void)pressX; (void)pressY;
                break;
            case SDL_EVENT_MOUSE_WHEEL: wheel += ev.wheel.y; break;
            // finger coordinates are normalized 0..1
            case SDL_EVENT_FINGER_DOWN: touch.down((int64_t)ev.tfinger.fingerID, ev.tfinger.x * pw, ev.tfinger.y * ph, now_s); break;
            case SDL_EVENT_FINGER_MOTION: touch.move((int64_t)ev.tfinger.fingerID, ev.tfinger.x * pw, ev.tfinger.y * ph, now_s); break;
            case SDL_EVENT_FINGER_UP: touch.up((int64_t)ev.tfinger.fingerID, ev.tfinger.x * pw, ev.tfinger.y * ph, now_s); break;
            case SDL_EVENT_FINGER_CANCELED: touch.cancelAll(); break;
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
#ifndef SDL_PLATFORM_ANDROID
            plat.fullscreen = !plat.fullscreen;
            SDL_SetWindowFullscreen(win, plat.fullscreen);
#endif
        }

        InputState in;
        const bool* k = SDL_GetKeyboardState(nullptr);
        in.held[ActLeft] = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A];
        in.held[ActRight] = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
        in.held[ActUp] = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W];
        in.held[ActDown] = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
        in.held[ActDrop] = k[SDL_SCANCODE_SPACE];
        in.held[ActConfirm] = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
        in.held[ActBack] = k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_AC_BACK] || backKey;
        in.held[ActPause] = k[SDL_SCANCODE_P];
        in.held[ActHelp] = k[SDL_SCANCODE_F1] || k[SDL_SCANCODE_H];
        in.held[ActZoomIn] = k[SDL_SCANCODE_Q];
        in.held[ActZoomOut] = k[SDL_SCANCODE_E];
        in.held[ActPrevTheme] = k[SDL_SCANCODE_LEFTBRACKET];
        in.held[ActNextTheme] = k[SDL_SCANCODE_RIGHTBRACKET];
        in.lookDX = mouseDX / (float)(wh > 0 ? wh : 1);
        in.lookDY = mouseDY / (float)(wh > 0 ? wh : 1);
        in.zoomSteps = wheel;
        in.lookReset = lookReset || k[SDL_SCANCODE_X];
        in.lookRateX = (k[SDL_SCANCODE_C] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_Z] ? 1.0f : 0.0f);
        if (mouseTap) { in.tap = true; in.tapX = tapX; in.tapY = tapY; }
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
        touch.apply(in, now_s);

        Uint64 now = SDL_GetTicksNS();
        float dt = (now - last) / 1e9f;
        last = now;
        renderer.setViewport(pw, ph);
        if (!host.frame(dt, in)) running = false;
        SDL_GL_SwapWindow(win);
    }
    for (SDL_Gamepad* g : pads) SDL_CloseGamepad(g);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
