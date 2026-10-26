// Procedural asset generation (textures, meshes). CPU only, portable.
#pragma once
#include "platform.h"
#include <cstdint>
#include <atomic>
#include <vector>

namespace hx {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // RGBA8, sRGB
    void init(int W, int H) { w = W; h = H; px.assign((size_t)W * H * 4, 0); }
    uint8_t* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
};

struct Rgb { float r, g, b; };

// Face color of each value 1..6 (sRGB 0..1), matching the original game.
Rgb diceColor(int value);

// 3x2 atlas, tile for value v at column (v-1)%3, row (v-1)/3.
Image makeDiceAtlas(int tile);
void diceTileUV(int value, float& u0, float& v0, float& u1, float& v1);

// Rounded cube of size 1 centered at origin. Local faces map to values like
// Orient: +Z=1, -Z=6, +Y=2, -Y=5, +X=3, -X=4.
void buildRoundedCube(float radius, int bevelSteps, std::vector<MeshVertex>& v, std::vector<uint16_t>& idx);

// Sprite atlas: 4x4 tiles.
enum Sprite {
    SprDot = 0, SprSparkle, SprRing, SprWhite,
    SprSpade, SprHeart, SprDiamond, SprClub,
    SprCoin, SprBubble, SprStreak, SprArrow,
    SprGradient, SprHex, SprSoftSquare, SprSmoke
};
Image makeSpriteAtlas();
void spriteUV(int sprite, float& u0, float& v0, float& u1, float& v1);

// One grid cell, meant to be repeated. White lines; tint via vertex color.
Image makeGridCell(int size);

// Theme backgrounds.
// cancel: checked per row; when it turns true the image is left unfinished.
Image makeBackground(int theme, int w, int h, const std::atomic<bool>* cancel = nullptr);

} // namespace hx
