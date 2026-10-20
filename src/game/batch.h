// Collects textured, colored triangles and submits them to the Renderer in as
// few draw calls as possible (flushes whenever the render state changes).
#pragma once
#include "hxmath.h"
#include "platform.h"
#include <vector>

namespace hx {

struct Color8 {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};
inline Color8 rgba(float r, float g, float b, float a = 1) {
    Color8 c;
    c.r = (uint8_t)(clampf(r, 0, 1) * 255 + 0.5f);
    c.g = (uint8_t)(clampf(g, 0, 1) * 255 + 0.5f);
    c.b = (uint8_t)(clampf(b, 0, 1) * 255 + 0.5f);
    c.a = (uint8_t)(clampf(a, 0, 1) * 255 + 0.5f);
    return c;
}

class Batch {
public:
    explicit Batch(Renderer* r) : r_(r) {}

    void begin(TexId tex, Blend blend, bool world, bool depthTest = false, float intensity = 1.0f) {
        if (tex != tex_ || blend != blend_ || world != world_ || depthTest != depth_ || intensity != intensity_) {
            flush();
            tex_ = tex; blend_ = blend; world_ = world; depth_ = depthTest; intensity_ = intensity;
        }
    }

    void vert(float x, float y, float z, float u, float v, Color8 c) {
        QuadVertex q;
        q.x = x; q.y = y; q.z = z; q.u = u; q.v = v;
        q.r = c.r; q.g = c.g; q.b = c.b; q.a = c.a;
        verts_.push_back(q);
    }

    // Axis-aligned rectangle (screen: y down; world: on the XY plane at z).
    void rect(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color8 c, float z = 0) {
        vert(x0, y0, z, u0, v0, c); vert(x1, y0, z, u1, v0, c); vert(x1, y1, z, u1, v1, c);
        vert(x0, y0, z, u0, v0, c); vert(x1, y1, z, u1, v1, c); vert(x0, y1, z, u0, v1, c);
    }

    // Oriented quad: center +/- right +/- up. uv0 maps to (-right, +up) corner.
    void quad(const Vec3& c, const Vec3& right, const Vec3& up, float u0, float v0, float u1, float v1, Color8 col) {
        Vec3 a = c - right + up, b = c + right + up, d = c + right - up, e = c - right - up;
        vert(a.x, a.y, a.z, u0, v0, col); vert(b.x, b.y, b.z, u1, v0, col); vert(d.x, d.y, d.z, u1, v1, col);
        vert(a.x, a.y, a.z, u0, v0, col); vert(d.x, d.y, d.z, u1, v1, col); vert(e.x, e.y, e.z, u0, v1, col);
    }

    void flush() {
        if (!verts_.empty())
            r_->drawTriangles(verts_.data(), (int)verts_.size(), tex_, blend_, world_, depth_, intensity_);
        verts_.clear();
    }

    Renderer* renderer() const { return r_; }

private:
    Renderer* r_;
    std::vector<QuadVertex> verts_;
    TexId tex_ = 0;
    Blend blend_ = Blend::Alpha;
    bool world_ = false, depth_ = false;
    float intensity_ = 1;
};

} // namespace hx
