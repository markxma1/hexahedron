// The ONLY interface a platform backend must implement.
// Everything else (rules, animation, particles, HUD, menus, fonts, audio synthesis)
// is portable C++ in src/core and src/game.
//
// A backend (SDL3+OpenGL, PS3/PSL1GHT, Godot GDExtension, ...) provides:
//   Renderer : upload RGBA8 textures + meshes, draw them in call order
//   Audio    : accept interleaved stereo float PCM
//   Files    : read asset files, load/save small text files (scores/settings)
//   Input    : current held state of abstract actions
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hx {

using TexId = uint32_t;
using MeshId = uint32_t;

struct MeshVertex { float px, py, pz, nx, ny, nz, u, v; };

// Color as 4 bytes (r,g,b,a) in memory order: endian-neutral.
struct QuadVertex { float x, y, z, u, v; uint8_t r, g, b, a; };

enum class Blend : uint8_t { Alpha, Additive };

struct MeshMaterial {
    TexId tex = 0;
    float tint[4] = {1, 1, 1, 1};    // multiplies texture; a < 1 = transparent
    float emissive[3] = {0, 0, 0};   // added after lighting (HDR, can exceed 1)
    float specular = 0.6f;
    float shininess = 48.0f;
};

struct FrameParams {
    float view[16], proj[16];
    float camPos[3];
    float lightDir[3];               // direction TO the light, normalized
    float lightColor[3];
    float skyColor[3], groundColor[3]; // hemisphere ambient
    float clearColor[3];
    float bloomStrength = 0.8f;      // backends without post-fx may ignore these
    float bloomThreshold = 1.0f;
    float exposure = 1.0f;
    float vignette = 0.35f;
};

class Renderer {
public:
    virtual ~Renderer() {}
    virtual TexId createTexture(int w, int h, const uint8_t* rgba, bool mipmaps, bool repeat) = 0;
    virtual void destroyTexture(TexId t) = 0;
    virtual MeshId createMesh(const MeshVertex* v, int nv, const uint16_t* idx, int ni) = 0;
    virtual void viewportSize(int& w, int& h) = 0;

    // Frame: beginFrame -> 3D drawing -> beginOverlay (post-fx applied) -> 2D -> endFrame
    virtual void beginFrame(const FrameParams& fp) = 0;
    // Lit mesh. Transparent if tint[3] < 1 (no depth write).
    virtual void drawMesh(MeshId mesh, const float model[16], const MeshMaterial& mat) = 0;
    // Unlit triangles (count = vertex count, multiple of 3).
    // world=true: uses the frame's view/proj. world=false: pixel coords, (0,0)=top-left.
    // intensity scales the color (HDR, > 1 makes it bloom) - world only.
    // Textures and vertex colors are sRGB.
    virtual void drawTriangles(const QuadVertex* v, int count, TexId tex, Blend blend,
                               bool world, bool depthTest, float intensity) = 0;
    virtual void beginOverlay() = 0;
    virtual void endFrame() = 0;
    // Optional: finished frame as RGB, top row first (screenshots for tests).
    virtual bool readPixels(std::vector<uint8_t>& rgb, int& w, int& h) { (void)rgb; (void)w; (void)h; return false; }
};

class Audio {
public:
    virtual ~Audio() {}
    virtual int sampleRate() = 0;
    virtual int queuedFrames() = 0;
    virtual void push(const float* stereo, int frames) = 0;
};

class Files {
public:
    virtual ~Files() {}
    virtual bool readAsset(const std::string& name, std::vector<uint8_t>& out) = 0;
    virtual bool loadText(const std::string& name, std::string& out) = 0;
    virtual bool saveText(const std::string& name, const std::string& data) = 0;
};

enum Action : int {
    ActLeft, ActRight, ActUp, ActDown,
    ActDrop,        // hard drop (fast roll to the bottom)
    ActConfirm, ActBack, ActPause, ActHelp,
    ActZoomIn, ActZoomOut, ActPrevTheme, ActNextTheme,
    ActCount
};

struct InputState {
    bool held[ActCount] = {};
    // Camera look (optional; a backend without mouse/stick just leaves these 0).
    float lookDX = 0, lookDY = 0;   // drag this frame, in screen heights (mouse)
    float lookRateX = 0, lookRateY = 0; // held rate -1..1 (keys / right stick)
    float zoomSteps = 0;            // mouse wheel notches / pinch, + = closer
    bool lookReset = false;
    // Pointer tap (mouse click without drag, or finger tap) in pixels, (0,0)=top-left.
    // Menus and on-screen buttons react to it.
    bool tap = false;
    float tapX = 0, tapY = 0;
    bool touchUi = false;           // last input came from a touch screen: show touch buttons
};

struct Platform {
    Renderer* renderer = nullptr;
    Audio* audio = nullptr;
    Files* files = nullptr;
    bool requestQuit = false;
    bool requestFullscreenToggle = false;
    bool fullscreen = false;
    bool hasWindow = true;          // false on consoles/phones: always fullscreen, no toggle
    // Screen edges the game should keep buttons away from (notch, system gestures), pixels.
    int safeLeft = 0, safeTop = 0, safeRight = 0, safeBottom = 0;
};

} // namespace hx
