// Simple computer player: attract-mode demo on the title screen and a
// smoke-test driver (--autoplay). Uses only the public core API.
#pragma once
#include "../core/game.h"

namespace hx {

class Autopilot {
public:
    explicit Autopilot(uint64_t seed = 42) : rng_(seed) {}
    Input think(const Game& g, float dt);
    float skill = 1.0f; // 0..1, lower = more random choices

private:
    int bestColumn(const Game& g);
    float evaluate(const Game& g, int col, bool& reachable);
    Rng rng_;
    uint32_t uid_ = 0;
    int target_ = -1;
    float replan_ = 0, pressCooldown_ = 0, dropDelay_ = 0;
};

} // namespace hx
