#include "font.h"
#define STB_TRUETYPE_IMPLEMENTATION

#include "../../third_party/stb/stb_truetype.h"

namespace hx {

static uint32_t nextCodepoint(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && i < s.size()) return ((c & 0x1F) << 6) | ((unsigned char)s[i++] & 0x3F);
    if ((c & 0xF0) == 0xE0 && i + 1 < s.size()) {
        uint32_t cp = ((c & 0x0F) << 12) | (((unsigned char)s[i] & 0x3F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
        i += 2;
        return cp;
    }
    return '?';
}

bool Font::load(Renderer* r, const std::vector<uint8_t>& ttf, float bakePx) {
    bakePx_ = bakePx;
    const int W = 2048, H = 2048;
    std::vector<uint8_t> alpha(W * H, 0);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, alpha.data(), W, H, 0, 2, nullptr)) return false;
    stbtt_PackSetOversampling(&pc, 2, 2);
    stbtt_packedchar pcs[224];
    if (!stbtt_PackFontRange(&pc, ttf.data(), 0, bakePx, 32, 224, pcs)) { stbtt_PackEnd(&pc); return false; }
    stbtt_PackEnd(&pc);

    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0));
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    float sc = stbtt_ScaleForPixelHeight(&info, bakePx);
    ascent_ = asc * sc;
    descent_ = desc * sc;

    glyphs_.resize(224);
    for (int i = 0; i < 224; ++i) {
        float x = 0, y = 0;
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(pcs, W, H, i, &x, &y, &q, 0);
        Glyph& g = glyphs_[i];
        g.x0 = q.x0; g.y0 = q.y0 + ascent_; g.x1 = q.x1; g.y1 = q.y1 + ascent_;
        g.u0 = q.s0; g.v0 = q.t0; g.u1 = q.s1; g.v1 = q.t1;
        g.adv = pcs[i].xadvance;
    }
    std::vector<uint8_t> rgba(W * H * 4);
    for (int i = 0; i < W * H; ++i) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    tex_ = r->createTexture(W, H, rgba.data(), true, false);
    return true;
}

const Font::Glyph* Font::glyph(uint32_t cp) const {
    if (cp < 32 || cp > 255) cp = '?';
    return &glyphs_[cp - 32];
}

float Font::width(const std::string& s, float size) const {
    float k = size / bakePx_, w = 0;
    for (size_t i = 0; i < s.size();) w += glyph(nextCodepoint(s, i))->adv * k;
    return w;
}

void Font::draw(Batch& b, const std::string& s, float x, float y, float size, Color8 c, Align align, float tracking) const {
    if (glyphs_.empty()) return;
    float k = size / bakePx_;
    int n = 0;
    for (size_t i = 0; i < s.size(); ++n) nextCodepoint(s, i);
    float w = width(s, size) + tracking * (n > 0 ? n - 1 : 0);
    if (align == Align::Center) x -= w * 0.5f;
    else if (align == Align::Right) x -= w;
    b.begin(tex_, Blend::Alpha, false);
    for (size_t i = 0; i < s.size();) {
        const Glyph* g = glyph(nextCodepoint(s, i));
        if (g->x1 > g->x0)
            b.rect(x + g->x0 * k, y + g->y0 * k, x + g->x1 * k, y + g->y1 * k, g->u0, g->v0, g->u1, g->v1, c);
        x += g->adv * k + tracking;
    }
}

void Font::drawShadowed(Batch& b, const std::string& s, float x, float y, float size, Color8 c, Align align, float tracking) const {
    float o = size * 0.06f + 1;
    Color8 sh = rgba(0, 0, 0, c.a / 255.0f * 0.75f);
    draw(b, s, x + o, y + o, size, sh, align, tracking);
    draw(b, s, x, y, size, c, align, tracking);
}

} // namespace hx
