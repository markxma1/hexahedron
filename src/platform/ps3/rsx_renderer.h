// PS3 (RSX) implementation of hx::Renderer. Same pipeline as the PC renderer:
// 3D into an FP16 HDR target, bloom chain, ACES tonemap, then 2D on top.
#pragma once
#include "../../game/platform.h"
#include <rsx/rsx.h>
#include <vector>

namespace hx {

class RSXRenderer : public Renderer {
public:
    bool init();

    TexId createTexture(int w, int h, const uint8_t* rgba, bool mipmaps, bool repeat) override;
    void destroyTexture(TexId t) override;
    MeshId createMesh(const MeshVertex* v, int nv, const uint16_t* idx, int ni) override;
    void viewportSize(int& w, int& h) override { w = (int)dispW_; h = (int)dispH_; }
    void beginFrame(const FrameParams& fp) override;
    void drawMesh(MeshId mesh, const float model[16], const MeshMaterial& mat) override;
    void drawTriangles(const QuadVertex* v, int count, TexId tex, Blend blend, bool world, bool depthTest, float intensity) override;
    void beginOverlay() override;
    void endFrame() override;
    bool readPixels(std::vector<uint8_t>& rgb, int& w, int& h) override;

private:
    struct Tex { u32 offset = 0, pitch = 0, format = 0; u16 w = 0, h = 0; bool repeat = false; void* mem = nullptr; };
    struct Mesh { u32 vb = 0, ib = 0; int count = 0; };
    struct Target { u32 offset = 0, pitch = 0; int w = 0, h = 0; void* mem = nullptr; };
    struct VProg { rsxVertexProgram* prog = nullptr; void* ucode = nullptr; };
    struct FProg { rsxFragmentProgram* prog = nullptr; void* ucode = nullptr; u32 size = 0; };
    struct RingPtr { void* ptr; u32 offset; };

    void loadVP(VProg& p, const void* data);
    void loadFP(FProg& p, const void* data);
    RingPtr ringAlloc(u32 size, u32 align);
    void useVP(VProg& p);
    VProg* curVP_ = nullptr;
    RingPtr bindFP(FProg& p);                              // per-draw copy of the program
    void fpConst(FProg& p, const RingPtr& copy, const char* name, const float* v);
    void vpConst(VProg& p, const char* name, const float* v);
    void vpMatrix(VProg& p, const char* name, const float* colMajor);
    void bindTexture(FProg& p, const char* sampler, const Tex& t, bool linearFilter);
    Tex targetTex(const Target& t) const;
    void setSurface(u32 colorOffset, u32 colorPitch, u32 colorFormat, int w, int h);
    void setViewport(int w, int h);
    void fullscreen(FProg& fp, u32 fpOff);
    Target makeTarget(int w, int h);
    void flip();

    gcmContextData* ctx_ = nullptr;
    u32 dispW_ = 1280, dispH_ = 720;
    u32 colorOffset_[2] = {}, colorPitch_ = 0;
    u32* colorBuf_[2] = {};
    u32 depthOffset_ = 0, depthPitch_ = 0;
    u32 curFb_ = 0;
    bool firstFlip_ = true;

    std::vector<Tex> texs_;
    std::vector<Mesh> meshes_;
    Target hdr_;
    static const int kBloomLevels = 5;
    Target bloom_[kBloomLevels];
    u32 fullVB_ = 0;                                       // fullscreen triangle

    VProg vpMesh_, vpSprite_, vpFull_;
    FProg fpMesh_, fpSprite_, fpBright_, fpDown_, fpUp_, fpComposite_;

    // Per-frame ring in RSX memory for vertices and fragment program copies.
    // Two halves: the GPU reads one while the CPU fills the other.
    static const u32 kRingHalf = 8 * 1024 * 1024;
    uint8_t* ring_ = nullptr;
    u32 ringOffset_ = 0, ringPos_ = 0, ringHalf_ = 0;
    bool ringWarned_ = false;

    FrameParams fp_;
    float viewProj_[16];
    bool inOverlay_ = false, noBlend_ = false;
    TexId whiteTex_ = 0;
};

} // namespace hx
