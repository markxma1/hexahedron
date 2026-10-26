#include "assets.h"
#include "hxmath.h"
#include <cmath>

namespace hx {

// ------------------------------------------------------------------ helpers

static inline uint8_t toByte(float v) {
    v = clampf(v, 0, 1);
    return (uint8_t)(v * 255.0f + 0.5f);
}
static inline void put(Image& im, int x, int y, float r, float g, float b, float a) {
    uint8_t* p = im.at(x, y);
    p[0] = toByte(r); p[1] = toByte(g); p[2] = toByte(b); p[3] = toByte(a);
}
static inline float sstep(float e0, float e1, float x) { return smooth((x - e0) / (e1 - e0)); }

static inline uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static inline float hashf(int x, int y, uint32_t seed) { return (hash2(x, y, seed) & 0xFFFFFF) / 16777215.0f; }

static float vnoise(float x, float y, uint32_t seed) {
    int ix = (int)std::floor(x), iy = (int)std::floor(y);
    float fx = x - ix, fy = y - iy;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    float a = hashf(ix, iy, seed), b = hashf(ix + 1, iy, seed);
    float c = hashf(ix, iy + 1, seed), d = hashf(ix + 1, iy + 1, seed);
    return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uy);
}
static float fbm(float x, float y, int oct, uint32_t seed) {
    float s = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < oct; ++i) {
        s += vnoise(x, y, seed + i * 31) * amp;
        norm += amp;
        x = x * 2.03f + 17.1f; y = y * 2.03f - 9.7f;
        amp *= 0.5f;
    }
    return s / norm;
}
static Rgb mix(Rgb a, Rgb b, float t) { return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)}; }
static Rgb add(Rgb a, Rgb b, float k) { return {a.r + b.r * k, a.g + b.g * k, a.b + b.b * k}; }
static Rgb mul(Rgb a, float k) { return {a.r * k, a.g * k, a.b * k}; }
static Rgb hsv(float h, float s, float v) {
    h = h - std::floor(h);
    float r = clampf(std::fabs(h * 6 - 3) - 1, 0, 1);
    float g = clampf(2 - std::fabs(h * 6 - 2), 0, 1);
    float b = clampf(2 - std::fabs(h * 6 - 4), 0, 1);
    return {v * lerpf(1, r, s), v * lerpf(1, g, s), v * lerpf(1, b, s)};
}

// ------------------------------------------------------------------ dice

Rgb diceColor(int value) {
    switch (value) {
    case 1: return {0.95f, 0.95f, 0.97f};
    case 2: return {0.98f, 0.84f, 0.16f};
    case 3: return {0.60f, 0.30f, 0.90f};
    case 4: return {0.22f, 0.58f, 0.96f};
    case 5: return {0.25f, 0.80f, 0.32f};
    case 6: return {0.92f, 0.20f, 0.20f};
    }
    return {1, 1, 1};
}

void diceTileUV(int value, float& u0, float& v0, float& u1, float& v1) {
    int c = (value - 1) % 3, r = (value - 1) / 3;
    u0 = c / 3.0f; u1 = (c + 1) / 3.0f;
    v0 = r / 2.0f; v1 = (r + 1) / 2.0f;
}

static int pipLayout(int v, float out[][2]) {
    const float a = 0.26f, b = 0.74f, m = 0.5f;
    int n = 0;
    auto P = [&](float x, float y) { out[n][0] = x; out[n][1] = y; ++n; };
    switch (v) {
    case 1: P(m, m); break;
    case 2: P(a, a); P(b, b); break;
    case 3: P(a, a); P(m, m); P(b, b); break;
    case 4: P(a, a); P(b, a); P(a, b); P(b, b); break;
    case 5: P(a, a); P(b, a); P(m, m); P(a, b); P(b, b); break;
    case 6: P(a, a); P(b, a); P(a, m); P(b, m); P(a, b); P(b, b); break;
    }
    return n;
}

Image makeDiceAtlas(int tile) {
    Image im;
    im.init(tile * 3, tile * 2);
    const int SS = 3; // supersampling for smooth pip edges
    for (int v = 1; v <= 6; ++v) {
        int tx = ((v - 1) % 3) * tile, ty = ((v - 1) / 3) * tile;
        Rgb base = diceColor(v);
        float pips[6][2];
        int np = pipLayout(v, pips);
        float pr = (v == 1) ? 0.15f : 0.085f;
        Rgb pipCol = (v == 1) ? Rgb{0.80f, 0.04f, 0.08f} : Rgb{0.05f, 0.05f, 0.07f};
        for (int y = 0; y < tile; ++y)
            for (int x = 0; x < tile; ++x) {
                Rgb acc{0, 0, 0};
                for (int sy = 0; sy < SS; ++sy)
                    for (int sx = 0; sx < SS; ++sx) {
                        float u = (x + (sx + 0.5f) / SS) / tile, w = (y + (sy + 0.5f) / SS) / tile;
                        float cx = u - 0.5f, cy = w - 0.5f;
                        // Face: soft center highlight, darker towards the bevel.
                        float maxn = std::fmax(std::fabs(cx), std::fabs(cy));
                        float shade = 1.0f + 0.10f * (1 - (cx * cx + cy * cy) * 4) - 0.18f * sstep(0.36f, 0.5f, maxn);
                        // Fine grain so faces do not look like flat plastic.
                        shade += (hashf(x, y, 7u + v) - 0.5f) * 0.025f;
                        Rgb c = mul(base, shade);
                        for (int i = 0; i < np; ++i) {
                            float dx = u - pips[i][0], dy = w - pips[i][1];
                            float d = std::sqrt(dx * dx + dy * dy);
                            if (d < pr) {
                                // Concave pip: dark rim top-left, soft light bottom-right.
                                float k = (dx + dy) / (pr * 1.414f);
                                float hl = 0.22f * clampf(k, 0, 1) * sstep(0.4f * pr, pr, d);
                                float rim = 1.0f - 0.5f * sstep(0.75f * pr, pr, d) * clampf(-k, 0, 1);
                                c = add(mul(pipCol, rim), Rgb{1, 1, 1}, hl * (v == 1 ? 0.6f : 1.0f));
                            } else if (d < pr + 0.018f) {
                                c = mul(c, 1.10f); // raised lip
                            }
                        }
                        acc = add(acc, c, 1.0f / (SS * SS));
                    }
                put(im, tx + x, ty + y, acc.r, acc.g, acc.b, 1);
            }
    }
    return im;
}

void buildRoundedCube(float radius, int k, std::vector<MeshVertex>& v, std::vector<uint16_t>& idx) {
    struct Face { Vec3 n, r, d; int value; };
    const Face faces[6] = {
        {Vec3(0, 0, 1), Vec3(1, 0, 0), Vec3(0, -1, 0), 1},
        {Vec3(0, 0, -1), Vec3(-1, 0, 0), Vec3(0, -1, 0), 6},
        {Vec3(0, 1, 0), Vec3(1, 0, 0), Vec3(0, 0, 1), 2},
        {Vec3(0, -1, 0), Vec3(1, 0, 0), Vec3(0, 0, -1), 5},
        {Vec3(1, 0, 0), Vec3(0, 0, -1), Vec3(0, -1, 0), 3},
        {Vec3(-1, 0, 0), Vec3(0, 0, 1), Vec3(0, -1, 0), 4},
    };
    // Parameter positions along an edge: dense in the bevel band.
    std::vector<float> ps;
    for (int i = 0; i <= k; ++i) ps.push_back(radius * (1 - std::cos(i / (float)k * kPi * 0.5f)));
    for (int i = k; i >= 0; --i) ps.push_back(1 - radius * (1 - std::cos(i / (float)k * kPi * 0.5f)));
    const int n = (int)ps.size();
    const float in = 0.5f - radius;
    for (const Face& f : faces) {
        float u0, v0, u1, v1;
        diceTileUV(f.value, u0, v0, u1, v1);
        uint16_t base = (uint16_t)v.size();
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                float s = ps[i], t = ps[j];
                Vec3 p = f.n * 0.5f + f.r * (s - 0.5f) + f.d * (t - 0.5f);
                Vec3 c(clampf(p.x, -in, in), clampf(p.y, -in, in), clampf(p.z, -in, in));
                Vec3 nn = normalize(p - c);
                Vec3 q = c + nn * radius;
                MeshVertex mv;
                mv.px = q.x; mv.py = q.y; mv.pz = q.z;
                mv.nx = nn.x; mv.ny = nn.y; mv.nz = nn.z;
                mv.u = lerpf(u0, u1, s); mv.v = lerpf(v0, v1, t);
                v.push_back(mv);
            }
        for (int j = 0; j + 1 < n; ++j)
            for (int i = 0; i + 1 < n; ++i) {
                uint16_t a = base + j * n + i, b = a + 1, c = a + n, d = c + 1;
                idx.push_back(a); idx.push_back(c); idx.push_back(b);
                idx.push_back(b); idx.push_back(c); idx.push_back(d);
            }
    }
}

// ------------------------------------------------------------------ sprites

static float heartF(float x, float y) { // y up; <0 inside
    float a = x * x + y * y - 1;
    return a * a * a - x * x * y * y * y;
}

static float spriteAlpha(int s, float x, float y) { // x,y in [-1,1], y down
    float d = std::sqrt(x * x + y * y);
    switch (s) {
    case SprDot: return std::pow(clampf(1 - d, 0, 1), 2.2f);
    case SprSparkle: {
        float st = std::fmax(std::exp(-std::fabs(x) * 14 - y * y * 3), std::exp(-std::fabs(y) * 14 - x * x * 3));
        return clampf((st + std::pow(clampf(1 - d * 1.6f, 0, 1), 3)) * clampf(1 - d, 0, 1) * 1.4f, 0, 1);
    }
    case SprRing: { float t = (d - 0.78f) / 0.09f; return std::exp(-t * t); }
    case SprWhite: return 1;
    case SprHeart: return heartF(x * 1.25f, -y * 1.25f + 0.25f) <= 0 ? 1.f : 0.f;
    case SprSpade: {
        bool body = heartF(x * 1.25f, y * 1.25f + 0.15f) <= 0;
        bool stem = y > 0.35f && y < 0.95f && std::fabs(x) < 0.06f + (y - 0.35f) * 0.55f;
        return (body || stem) ? 1.f : 0.f;
    }
    case SprDiamond: return (std::fabs(x) / 0.65f + std::fabs(y) / 0.92f) <= 1 ? 1.f : 0.f;
    case SprClub: {
        auto circ = [&](float cx, float cy) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) < 0.33f * 0.33f; };
        bool stem = y > 0.1f && y < 0.95f && std::fabs(x) < 0.06f + (y - 0.1f) * 0.4f;
        return (circ(0, -0.42f) || circ(-0.38f, 0.1f) || circ(0.38f, 0.1f) || stem) ? 1.f : 0.f;
    }
    case SprCoin: return d < 0.88f ? (d > 0.72f ? 1.0f : 0.85f) : 0.f;
    case SprBubble: {
        float rim = std::exp(-std::pow((d - 0.86f) / 0.06f, 2));
        float hl = std::exp(-((x + 0.35f) * (x + 0.35f) + (y + 0.4f) * (y + 0.4f)) * 40);
        return clampf(rim + hl + (d < 0.86f ? 0.08f : 0), 0, 1);
    }
    case SprStreak: return std::exp(-std::fabs(y) * 9) * clampf(1.2f - x * x, 0, 1);
    case SprArrow: {
        float yy = y;
        bool head = yy > -0.9f && yy < 0.05f && std::fabs(x) <= (yy + 0.9f) * 0.95f;
        bool shaft = std::fabs(x) < 0.26f && yy >= 0.0f && yy < 0.9f;
        return (head || shaft) ? 1.f : 0.f;
    }
    case SprGradient: return clampf((y + 1) * 0.5f, 0, 1);
    case SprHex: {
        float hd = std::fmax(std::fabs(x) * 0.866f + std::fabs(y) * 0.5f, std::fabs(y));
        float t = (hd - 0.78f) / 0.07f;
        return std::exp(-t * t);
    }
    case SprSoftSquare: return 1 - sstep(0.55f, 1.0f, std::fmax(std::fabs(x), std::fabs(y)));
    case SprSmoke: return std::pow(clampf(1 - d, 0, 1), 1.5f) * (0.35f + 0.65f * fbm(x * 2.5f + 3, y * 2.5f + 7, 4, 99));
    }
    return 0;
}

Image makeSpriteAtlas() {
    const int T = 128, SS = 4;
    Image im;
    im.init(T * 4, T * 4);
    for (int s = 0; s < 16; ++s) {
        int ox = (s % 4) * T, oy = (s / 4) * T;
        for (int y = 0; y < T; ++y)
            for (int x = 0; x < T; ++x) {
                float a = 0;
                for (int sy = 0; sy < SS; ++sy)
                    for (int sx = 0; sx < SS; ++sx) {
                        float fx = ((x + (sx + 0.5f) / SS) / T) * 2 - 1;
                        float fy = ((y + (sy + 0.5f) / SS) / T) * 2 - 1;
                        a += spriteAlpha(s, fx, fy);
                    }
                a /= SS * SS;
                float lum = 1;
                if (s == SprCoin) { // embossed coin: shading in luminance
                    float fx = (x + 0.5f) / T * 2 - 1, fy = (y + 0.5f) / T * 2 - 1;
                    lum = 0.75f + 0.25f * clampf(-(fx + fy), -1, 1);
                }
                put(im, ox + x, oy + y, lum, lum, lum, a);
            }
    }
    return im;
}

void spriteUV(int s, float& u0, float& v0, float& u1, float& v1) {
    const float e = 0.5f / 512.0f; // half-texel inset
    u0 = (s % 4) / 4.0f + e; v0 = (s / 4) / 4.0f + e;
    u1 = (s % 4 + 1) / 4.0f - e; v1 = (s / 4 + 1) / 4.0f - e;
}

Image makeGridCell(int n) {
    Image im;
    im.init(n, n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = std::fmin((float)x + 0.5f, n - x - 0.5f);
            float dy = std::fmin((float)y + 0.5f, n - y - 0.5f);
            float dmin = std::fmin(dx, dy);
            float line = std::exp(-(dmin * dmin) / 2.0f) + 0.35f * std::exp(-dmin / 5.0f);
            float corner = std::exp(-(dx * dx + dy * dy) / 30.0f) * 0.9f;
            float fill = 0.035f + 0.02f * (float)y / n;
            put(im, x, y, 1, 1, 1, clampf(fill + line + corner, 0, 1));
        }
    return im;
}

// ------------------------------------------------------------------ backgrounds

static void stars(Image& im, uint32_t seed, int count, float bright) {
    for (int i = 0; i < count; ++i) {
        float sx = hashf(i, 1, seed) * im.w, sy = hashf(i, 2, seed) * im.h;
        float b = std::pow(hashf(i, 3, seed), 3.0f) * bright + 0.15f;
        float rad = 0.6f + hashf(i, 4, seed) * 1.4f;
        int r = (int)(rad * 3) + 1;
        for (int y = (int)sy - r; y <= (int)sy + r; ++y)
            for (int x = (int)sx - r; x <= (int)sx + r; ++x) {
                if (x < 0 || y < 0 || x >= im.w || y >= im.h) continue;
                float dd = ((x - sx) * (x - sx) + (y - sy) * (y - sy)) / (rad * rad);
                float a = std::exp(-dd * 1.5f) * b;
                uint8_t* p = im.at(x, y);
                for (int k = 0; k < 3; ++k) p[k] = toByte(p[k] / 255.0f + a);
            }
    }
}

// Shaded sphere (planet / moon) composited over the image.
static void planet(Image& im, float cx, float cy, float rad, Rgb c1, Rgb c2, Rgb atmo, uint32_t seed) {
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            float dx = (x - cx) / rad, dy = (y - cy) / rad, d2 = dx * dx + dy * dy;
            uint8_t* p = im.at(x, y);
            Rgb cur{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f};
            float d = std::sqrt(d2);
            if (d < 1) {
                float nz = std::sqrt(1 - d2);
                float lit = clampf(-dx * 0.55f - dy * 0.45f + nz * 0.7f, 0, 1);
                float n = fbm(dx * 3 + 5, dy * 6 + 2, 5, seed);
                Rgb s = mix(c1, c2, n);
                s = mul(s, 0.05f + lit * 0.95f);
                float rim = std::pow(1 - nz, 3) * 0.8f;
                s = add(s, atmo, rim * lit);
                float aa = clampf((1 - d) * rad, 0, 1);
                cur = mix(cur, s, aa);
            } else {
                float glow = std::exp(-(d - 1) * 14) * 0.55f;
                cur = add(cur, atmo, glow);
            }
            put(im, x, y, cur.r, cur.g, cur.b, 1);
        }
}

Image makeBackground(int theme, int W, int H, const std::atomic<bool>* cancel) {
    Image im;
    im.init(W, H);
    const float asp = (float)W / H;
    for (int y = 0; y < H; ++y) {
        if (cancel && *cancel) return im;
        for (int x = 0; x < W; ++x) {
            float u = (float)x / W, v = (float)y / H;
            float px = u * asp, py = v;
            Rgb c{0, 0, 0};
            switch (theme) {
            case 0: { // SPACE-DIE VEST: nebula
                float wx = fbm(px * 2.2f, py * 2.2f, 4, 11), wy = fbm(px * 2.2f + 5, py * 2.2f + 3, 4, 12);
                float n = fbm(px * 2.5f + wx * 1.8f, py * 2.5f + wy * 1.8f, 6, 13);
                float m = fbm(px * 1.5f + 9, py * 1.5f, 4, 14);
                Rgb neb = mix(Rgb{0.10f, 0.25f, 0.85f}, Rgb{0.75f, 0.12f, 0.65f}, sstep(0.35f, 0.65f, m));
                c = mix(Rgb{0.01f, 0.012f, 0.05f}, Rgb{0.03f, 0.02f, 0.10f}, v);
                c = add(c, neb, std::pow(sstep(0.35f, 0.85f, n), 2.2f) * 0.9f);
                c = add(c, Rgb{0.9f, 0.6f, 1.0f}, std::pow(sstep(0.62f, 0.95f, n), 3.0f) * 0.5f);
                break;
            }
            case 1: { // VERTERE: vortex
                float dx = (u - 0.5f) * asp, dy = v - 0.5f;
                float r = std::sqrt(dx * dx + dy * dy) + 1e-4f, a = std::atan2(dy, dx);
                float sw = a + std::log(r) * 2.4f;
                float n = fbm(std::cos(sw) * 2 + r * 3, std::sin(sw) * 2 + r * 3, 5, 21);
                float bands = 0.5f + 0.5f * std::sin(sw * 3 + n * 6);
                c = mix(Rgb{0.01f, 0.06f, 0.08f}, Rgb{0.03f, 0.35f, 0.38f}, bands * sstep(0.0f, 0.6f, r));
                c = add(c, Rgb{0.95f, 0.45f, 0.10f}, std::pow(bands, 6) * n * 0.6f * sstep(0.05f, 0.5f, r));
                c = mul(c, 0.35f + 0.65f * sstep(0.02f, 0.4f, r));
                break;
            }
            case 2: { // SLEEPY SPUDGY: dreamy night with hills
                c = mix(Rgb{0.04f, 0.03f, 0.14f}, Rgb{0.30f, 0.16f, 0.38f}, std::pow(v, 1.6f));
                float cl = fbm(px * 3 + 2, py * 7, 5, 31);
                c = add(c, Rgb{0.55f, 0.45f, 0.75f}, sstep(0.55f, 0.8f, cl) * 0.25f * (1 - v * 0.5f));
                float h1 = 0.72f + 0.05f * std::sin(px * 5.0f) + 0.03f * std::sin(px * 13 + 1);
                float h2 = 0.82f + 0.04f * std::sin(px * 3.0f + 2) + 0.02f * std::sin(px * 17);
                if (v > h1) c = mix(c, Rgb{0.10f, 0.05f, 0.18f}, 0.9f);
                if (v > h2) c = mix(c, Rgb{0.04f, 0.02f, 0.08f}, 0.95f);
                break;
            }
            case 3: { // TORNADO CARD: green felt with a swirl
                float dx = (u - 0.5f) * asp, dy = v - 0.5f;
                float r = std::sqrt(dx * dx + dy * dy), a = std::atan2(dy, dx);
                float felt = fbm(px * 60, py * 60, 2, 41) * 0.08f;
                c = mix(Rgb{0.02f, 0.20f, 0.10f}, Rgb{0.0f, 0.05f, 0.03f}, sstep(0.1f, 0.9f, r));
                c = add(c, Rgb{1, 1, 1}, felt - 0.04f);
                float sw = 0.5f + 0.5f * std::sin(a * 2 + r * 14 + fbm(px * 3, py * 3, 3, 42) * 3);
                c = add(c, Rgb{0.85f, 0.75f, 0.35f}, std::pow(sw, 12) * 0.25f * (1 - sstep(0.1f, 0.8f, r)));
                break;
            }
            case 4: { // TIE DIE HIPPIE: psychedelic spiral
                float dx = (u - 0.45f) * asp, dy = v - 0.55f;
                float r = std::sqrt(dx * dx + dy * dy) + 1e-4f, a = std::atan2(dy, dx);
                float n = fbm(px * 4, py * 4, 4, 51);
                float hue = a / (2 * kPi) * 2 + std::log(r) * 0.9f + n * 0.5f;
                float crinkle = 0.75f + 0.25f * fbm(px * 25, py * 25, 3, 52);
                c = hsv(hue, 0.7f, 0.36f * crinkle);
                break;
            }
            case 5: { // MERCURY 42: liquid chrome
                auto field = [&](float qx, float qy) {
                    float f = fbm(qx * 1.6f, qy * 1.6f, 4, 61);
                    return sstep(0.38f, 0.62f, f) * 0.8f + fbm(qx * 6, qy * 6, 3, 62) * 0.06f;
                };
                float e = 0.004f;
                float h0 = field(px, py);
                float hx = field(px + e, py), hy = field(px, py + e);
                Vec3 n = normalize(Vec3((h0 - hx) / e * 0.06f, (h0 - hy) / e * 0.06f, 1));
                // Studio environment: soft bands = chrome look.
                float band = 0.5f + 0.5f * std::sin((n.x * 2.0f + n.y * 4.0f) * 3.0f + 1.0f);
                Rgb env = mix(Rgb{0.03f, 0.04f, 0.06f}, Rgb{0.62f, 0.70f, 0.78f}, std::pow(band, 1.5f));
                float spec = std::pow(clampf(dot(n, normalize(Vec3(-0.4f, -0.5f, 0.8f))), 0, 1), 60);
                Rgb pool = mix(Rgb{0.02f, 0.03f, 0.05f}, Rgb{0.06f, 0.09f, 0.12f}, v);
                c = mix(pool, add(env, Rgb{0.5f, 0.95f, 1.0f}, spec * 0.8f), sstep(0.05f, 0.4f, h0));
                c = mul(c, 0.8f);
                break;
            }
            case 6: { // BIG BUSINESS: night skyline
                c = mix(Rgb{0.01f, 0.02f, 0.06f}, Rgb{0.50f, 0.25f, 0.08f}, std::pow(v, 2.5f));
                int bw = W / 22;
                int col = x / bw;
                float bh = 0.25f + 0.5f * hashf(col, 0, 71) * (0.4f + 0.6f * hashf(col, 1, 71));
                int inset = (int)(hashf(col, 2, 71) * bw * 0.25f);
                int lx = x - col * bw;
                if (v > 1 - bh && lx >= inset && lx < bw - inset / 2) {
                    float shade = 0.03f + 0.03f * hashf(col, 3, 71);
                    c = Rgb{shade, shade * 1.1f, shade * 1.6f};
                    int wx = lx - inset, wy = y - (int)((1 - bh) * H);
                    bool cellX = (wx % 9) >= 3 && (wx % 9) <= 6;
                    bool cellY = (wy % 13) >= 4 && (wy % 13) <= 9;
                    int wid = wx / 9, hid = wy / 13;
                    if (cellX && cellY && wy > 6 && hashf(col * 64 + wid, hid, 72) > 0.55f) {
                        Rgb wc = hashf(col * 64 + wid, hid, 73) > 0.8f ? Rgb{0.6f, 0.8f, 1.0f} : Rgb{1.0f, 0.75f, 0.3f};
                        c = mul(wc, 0.3f + 0.6f * hashf(col * 64 + wid, hid, 74));
                    }
                }
                break;
            }
            }
            put(im, x, y, c.r, c.g, c.b, 1);
        }
    }
    if (cancel && *cancel) return im;
    if (theme == 0) {
        stars(im, 5, 900, 0.9f);
        planet(im, W * 0.83f, H * 1.08f, H * 0.42f, Rgb{0.55f, 0.25f, 0.55f}, Rgb{0.95f, 0.70f, 0.80f}, Rgb{1.0f, 0.5f, 0.8f}, 15);
        planet(im, W * 0.88f, H * 0.12f, H * 0.08f, Rgb{0.35f, 0.33f, 0.32f}, Rgb{0.70f, 0.68f, 0.66f}, Rgb{0.6f, 0.7f, 1.0f}, 16);
    } else if (theme == 2) {
        stars(im, 6, 300, 0.6f);
        planet(im, W * 0.2f, H * 0.2f, H * 0.1f, Rgb{0.95f, 0.92f, 0.80f}, Rgb{0.85f, 0.82f, 0.72f}, Rgb{1.0f, 0.95f, 0.8f}, 33);
    } else if (theme == 6) {
        stars(im, 7, 120, 0.4f);
    }
    return im;
}

} // namespace hx
