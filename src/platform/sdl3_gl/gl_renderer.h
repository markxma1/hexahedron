// OpenGL 3.3 implementation of hx::Renderer with HDR, MSAA, bloom and tone mapping.
#pragma once
#include "../../game/platform.h"
#include "gl_funcs.h"
#include <vector>

namespace hx {

class GLRenderer : public Renderer {
public:
    bool init(int msaa);
    void setViewport(int w, int h) { width_ = w; height_ = h; }

    TexId createTexture(int w, int h, const uint8_t* rgba, bool mipmaps, bool repeat) override;
    void destroyTexture(TexId t) override;
    MeshId createMesh(const MeshVertex* v, int nv, const uint16_t* idx, int ni) override;
    void viewportSize(int& w, int& h) override { w = width_; h = height_; }
    void beginFrame(const FrameParams& fp) override;
    void drawMesh(MeshId mesh, const float model[16], const MeshMaterial& mat) override;
    void drawTriangles(const QuadVertex* v, int count, TexId tex, Blend blend, bool world, bool depthTest, float intensity) override;
    void beginOverlay() override;
    void endFrame() override;

    // Reads the finished frame (RGB, top row first).
    void readPixels(std::vector<uint8_t>& rgb);

private:
    struct Mesh { GLuint vao, vbo, ibo; int count; };
    struct Target { GLuint fbo = 0, tex = 0; int w = 0, h = 0; };
    void ensureTargets();
    void makeTarget(Target& t, int w, int h);
    void freeTarget(Target& t);
    void fullscreenPass(GLuint prog, GLuint tex, Target* dst, int w, int h);
    GLuint compile(const char* vs, const char* fs);

    int width_ = 1, height_ = 1, tw_ = 0, th_ = 0, msaa_ = 4;
    std::vector<Mesh> meshes_;
    GLuint progMesh_ = 0, progSprite_ = 0, progBright_ = 0, progDown_ = 0, progUp_ = 0, progComposite_ = 0;
    GLuint spriteVao_ = 0, spriteVbo_ = 0, quadVao_ = 0;
    GLuint msFbo_ = 0, msColor_ = 0, msDepth_ = 0;
    Target hdr_;
    static const int kBloomLevels = 6;
    Target bloom_[kBloomLevels];
    FrameParams fp_;
    float viewProj_[16];
    bool inOverlay_ = false;
};

} // namespace hx
