// Visual themes; the theme changes with the level (optional).
#pragma once
#include "assets.h"

namespace hx {

enum class BgFx { Stars, Spiral, Fireflies, Cards, Bubbles, Chrome, Coins };

struct Theme {
    const char* name;
    Rgb accent;       // grid lines / frame
    Rgb wipe;         // wipe line color
    Rgb sky, ground;  // hemisphere ambient for the dice
    float bloom;
    BgFx fx;
};

static const int kThemeCount = 7;

inline const Theme& theme(int i) {
    static const Theme t[kThemeCount] = {
        {"DEEP SPACE", {0.35f, 0.65f, 1.0f}, {0.6f, 0.85f, 1.0f}, {0.42f, 0.45f, 0.62f}, {0.16f, 0.10f, 0.20f}, 0.9f, BgFx::Stars},
        {"VORTEX", {0.2f, 0.95f, 0.85f}, {1.0f, 0.65f, 0.25f}, {0.35f, 0.50f, 0.50f}, {0.20f, 0.12f, 0.06f}, 0.9f, BgFx::Spiral},
        {"FIREFLY NIGHT", {0.75f, 0.6f, 1.0f}, {1.0f, 0.9f, 0.6f}, {0.42f, 0.38f, 0.58f}, {0.14f, 0.08f, 0.18f}, 0.8f, BgFx::Fireflies},
        {"CARD STORM", {0.95f, 0.8f, 0.35f}, {1.0f, 0.95f, 0.7f}, {0.38f, 0.48f, 0.38f}, {0.06f, 0.16f, 0.08f}, 0.85f, BgFx::Cards},
        {"KALEIDOSCOPE", {1.0f, 0.5f, 0.9f}, {1.0f, 1.0f, 1.0f}, {0.50f, 0.42f, 0.48f}, {0.20f, 0.14f, 0.08f}, 0.8f, BgFx::Bubbles},
        {"QUICKSILVER", {0.7f, 0.85f, 1.0f}, {0.6f, 1.0f, 1.0f}, {0.50f, 0.55f, 0.60f}, {0.10f, 0.12f, 0.14f}, 1.0f, BgFx::Chrome},
        {"GOLD RUSH", {1.0f, 0.75f, 0.3f}, {1.0f, 0.85f, 0.45f}, {0.40f, 0.40f, 0.50f}, {0.22f, 0.12f, 0.05f}, 0.9f, BgFx::Coins},
    };
    return t[(i % kThemeCount + kThemeCount) % kThemeCount];
}

} // namespace hx
