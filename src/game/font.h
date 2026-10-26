// TrueType text via stb_truetype, baked once into an atlas (Latin-1 range).
#pragma once
#include "batch.h"
#include <string>
#include <vector>

namespace hx {

enum class Align { Left, Center, Right };

class Font {
public:
    bool load(Renderer* r, const std::vector<uint8_t>& ttf, float bakePx) { return bake(ttf, bakePx) && upload(r); }
    // Split for loading on a worker: bake() is CPU only, upload() needs the renderer.
    bool bake(const std::vector<uint8_t>& ttf, float bakePx);
    bool upload(Renderer* r);
    // size = cap-height-ish pixel size (the baked em size scaled).
    float width(const std::string& utf8, float size) const;
    // y is the top of the text line.
    void draw(Batch& b, const std::string& utf8, float x, float y, float size, Color8 c,
              Align align = Align::Left, float tracking = 0) const;
    // Text with a soft dark shadow, like the original's HUD.
    void drawShadowed(Batch& b, const std::string& utf8, float x, float y, float size, Color8 c,
                      Align align = Align::Left, float tracking = 0) const;
    float lineHeight(float size) const { return (ascent_ - descent_) * size / bakePx_; }
    TexId texture() const { return tex_; }

private:
    static const int kAtlas = 2048;
    struct Glyph { float x0, y0, x1, y1, u0, v0, u1, v1, adv; };
    std::vector<Glyph> glyphs_; // codepoints 32..255
    TexId tex_ = 0;
    std::vector<uint8_t> rgba_; // baked atlas until upload()
    float bakePx_ = 64, ascent_ = 0, descent_ = 0;
    const Glyph* glyph(uint32_t cp) const;
};

} // namespace hx
