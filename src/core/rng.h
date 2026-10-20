// Small deterministic RNG (xorshift64*), identical results on every platform.
#pragma once
#include <cstdint>

namespace hx {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint32_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return (uint32_t)((s * 0x2545F4914F6CDD1Dull) >> 32);
    }
    int range(int n) { return n > 0 ? (int)(next() % (uint32_t)n) : 0; } // [0,n)
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }      // [0,1)
};

} // namespace hx
