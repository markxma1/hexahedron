// Tests for the portable touch gesture recognizer (src/game/touch.h).
#include "game/touch.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace hx;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// Runs frames and counts rising edges ("presses") per action like the App does.
struct Rig {
    TouchGestures g;
    bool prev[ActCount] = {};
    int presses[ActCount] = {};
    bool tap = false; float tapX = 0, tapY = 0;
    float lookDX = 0, lookDY = 0, zoom = 0;
    bool lookReset = false, downHeld = false;
    double t = 0;
    Rig() { g.setScreen(1000, 1000); }
    void frame(int n = 1) {
        for (int i = 0; i < n; ++i) {
            t += 1.0 / 60.0;
            InputState in;
            g.apply(in, t);
            for (int a = 0; a < ActCount; ++a) {
                if (in.held[a] && !prev[a]) ++presses[a];
                prev[a] = in.held[a];
            }
            if (in.tap) { tap = true; tapX = in.tapX; tapY = in.tapY; }
            lookDX += in.lookDX; lookDY += in.lookDY; zoom += in.zoomSteps;
            lookReset |= in.lookReset;
            downHeld = in.held[ActDown];
        }
    }
};

int main() {
    const float u = 70; // step = 0.07 * 1000

    { // short tap
        Rig r;
        r.g.down(1, 500, 500, r.t); r.frame();
        r.g.up(1, 503, 502, r.t + 0.05); r.frame();
        CHECK(r.tap);
        CHECK(std::fabs(r.tapX - 503) < 0.01f && std::fabs(r.tapY - 502) < 0.01f);
        r.tap = false; r.frame(3);
        CHECK(!r.tap);
        for (int a = 0; a < ActCount; ++a) CHECK(r.presses[a] == 0);
    }
    { // long press without movement is not a tap
        Rig r;
        r.g.down(1, 500, 500, r.t); r.frame(60);
        r.g.up(1, 500, 500, r.t); r.frame();
        CHECK(!r.tap);
    }
    { // drag right 3.5 steps -> exactly 3 right presses, no tap
        Rig r;
        r.g.down(1, 100, 500, r.t); r.frame();
        for (int i = 1; i <= 35; ++i) { r.g.move(1, 100 + u * 0.1f * i, 500 + (i % 2), r.t); r.frame(); }
        r.g.up(1, 100 + u * 3.5f, 500, r.t); r.frame(10);
        CHECK(r.presses[ActRight] == 3);
        CHECK(r.presses[ActLeft] == 0);
        CHECK(!r.tap);
    }
    { // a fast drag left (all at once) still gives one press per step, separated by gaps
        Rig r;
        r.g.down(1, 500, 500, r.t); r.frame();
        r.g.move(1, 500 - u * 4.2f, 505, r.t); r.frame(20);
        CHECK(r.presses[ActLeft] == 4);
        CHECK(r.presses[ActDown] == 0 && r.presses[ActDrop] == 0);
        r.g.up(1, 500 - u * 4.2f, 505, r.t); r.frame();
    }
    { // drag up 2 steps -> 2 up presses
        Rig r;
        r.g.down(1, 500, 800, r.t); r.frame();
        for (int i = 1; i <= 20; ++i) { r.g.move(1, 500, 800 - u * 0.1f * i, r.t); r.frame(); }
        r.g.up(1, 500, 800 - u * 2, r.t); r.frame(10);
        CHECK(r.presses[ActUp] == 2);
        CHECK(r.presses[ActLeft] == 0 && r.presses[ActRight] == 0);
    }
    { // slow drag down -> Down held while the finger is down, no drop
        Rig r;
        r.g.down(1, 500, 200, r.t); r.frame();
        for (int i = 1; i <= 60; ++i) { r.g.move(1, 500, 200 + u * 2 * i / 60.0f, r.t); r.frame(); }
        CHECK(r.downHeld);
        r.frame(30); // finger resting, still held
        CHECK(r.downHeld);
        r.g.up(1, 500, 200 + u * 2, r.t); r.frame(5);
        CHECK(!r.downHeld);
        CHECK(r.presses[ActDrop] == 0);
    }
    { // fast flick down -> one drop press
        Rig r;
        r.g.down(1, 500, 200, r.t); r.frame();
        for (int i = 1; i <= 5; ++i) { r.g.move(1, 500, 200 + 50.0f * i, r.t); r.frame(); }
        r.g.up(1, 500, 450, r.t); r.frame(5);
        CHECK(r.presses[ActDrop] == 1);
        CHECK(!r.tap);
        CHECK(!r.downHeld);
    }
    { // two fingers: move together = look, spread = zoom in, no single-finger actions
        Rig r;
        r.g.down(1, 400, 500, r.t); r.g.down(2, 600, 500, r.t); r.frame();
        for (int i = 1; i <= 10; ++i) { r.g.move(1, 400 + 10.0f * i, 500, r.t); r.g.move(2, 600 + 10.0f * i, 500, r.t); r.frame(); }
        CHECK(std::fabs(r.lookDX - 0.1f) < 0.01f);
        CHECK(std::fabs(r.lookDY) < 0.01f);
        float z0 = r.zoom;
        for (int i = 1; i <= 10; ++i) { r.g.move(1, 500 - 10.0f * i, 500, r.t); r.g.move(2, 700 + 10.0f * i, 500, r.t); r.frame(); }
        CHECK(r.zoom - z0 > 1.0f);
        r.g.up(1, 400, 500, r.t); r.frame();
        r.g.move(2, 1000, 500, r.t); r.frame(5); // remaining finger is ignored
        r.g.up(2, 1000, 500, r.t); r.frame(5);
        for (int a = 0; a < ActCount; ++a) CHECK(r.presses[a] == 0);
        CHECK(!r.tap);
        CHECK(!r.lookReset);
    }
    { // two-finger tap -> look reset, no tap
        Rig r;
        r.g.down(1, 400, 500, r.t); r.g.down(2, 600, 500, r.t); r.frame();
        r.g.up(1, 401, 500, r.t + 0.05); r.g.up(2, 600, 501, r.t + 0.05); r.frame();
        CHECK(r.lookReset);
        CHECK(!r.tap);
    }
    { // apply() never clears what the keyboard already set
        TouchGestures g; g.setScreen(1000, 1000);
        InputState in; in.held[ActLeft] = true; in.tap = true; in.lookDX = 0.5f;
        g.apply(in, 1.0);
        CHECK(in.held[ActLeft] && in.tap && in.lookDX == 0.5f);
    }
    { // recentlyUsed
        TouchGestures g; g.setScreen(1000, 1000);
        CHECK(!g.recentlyUsed(0));
        g.down(1, 1, 1, 2.0); g.up(1, 1, 1, 2.1);
        CHECK(g.recentlyUsed(3.0));
        CHECK(!g.recentlyUsed(30.0));
    }
    if (fails) { std::printf("%d touch test(s) failed\n", fails); return 1; }
    std::printf("all touch tests passed\n");
    return 0;
}
