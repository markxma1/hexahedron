// Core rule tests. No framework; returns non-zero on failure.
#include "../src/core/game.h"
#include <cstdio>
#include <vector>

using namespace hx;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static void step(Game& g, float seconds, const Input& in = Input{}) {
    const float dt = 1.0f / 120.0f;
    for (float t = 0; t < seconds; t += dt) g.update(dt, in);
}

static bool has(const std::vector<Event>& ev, Ev t) {
    for (const Event& e : ev) if (e.type == t) return true;
    return false;
}

// Fresh game with an empty board, no active die, no rising dice soon.
static Game emptyGame(Mode m = Mode::Standard) {
    Game g(Config{}, m, 1234);
    g.debugRemoveActive();
    g.debugSetRiseTimer(1e9f, 0);
    g.takeEvents();
    return g;
}

static void testOrient() {
    // 24 distinct orientations, opposite faces always sum to 7.
    std::vector<Orient> all{Orient::identity()};
    for (size_t i = 0; i < all.size(); ++i)
        for (Roll r : {Roll::Down, Roll::Left, Roll::Right}) {
            Orient n = all[i].rolled(r);
            bool known = false;
            for (auto& k : all) if (k == n) known = true;
            if (!known) all.push_back(n);
        }
    CHECK(all.size() == 24);
    for (auto& o : all) {
        CHECK(o.faceValue(DirPX) + o.faceValue(DirNX) == 7);
        CHECK(o.faceValue(DirPY) + o.faceValue(DirNY) == 7);
        CHECK(o.faceValue(DirPZ) + o.faceValue(DirNZ) == 7);
        // Rolling semantics
        CHECK(o.rolled(Roll::Down).front() == o.faceValue(DirPY));
        CHECK(o.rolled(Roll::Right).front() == o.faceValue(DirNX));
        CHECK(o.rolled(Roll::Left).front() == o.faceValue(DirPX));
        CHECK(o.rolled(Roll::Left).rolled(Roll::Right) == o);
        // Four rolls down = full turn
        CHECK(o.rolled(Roll::Down).rolled(Roll::Down).rolled(Roll::Down).rolled(Roll::Down) == o);
    }
}

static void testComboForming() {
    Game g = emptyGame();
    g.debugPlace(0, 0, 3);
    g.debugPlace(1, 0, 3);
    g.debugRecompute();
    CHECK(g.cell(0, 0).combo < 0);          // two 3s: not enough
    g.debugPlace(1, 1, 3);                  // L-shape: three 3s
    g.debugRecompute();
    CHECK(g.cell(0, 0).combo >= 0);
    CHECK(g.cell(0, 0).state == DieState::Blue);
    CHECK(g.chain() == 3);
    CHECK(g.multiplier() == 3);
    // Ones never combo
    g.debugPlace(5, 0, 1);
    g.debugPlace(6, 0, 1);
    g.debugRecompute();
    CHECK(g.cell(5, 0).combo < 0);
    // Growing an existing combo adds chain
    g.debugPlace(2, 0, 3);
    g.debugRecompute();
    CHECK(g.cell(2, 0).combo == g.cell(0, 0).combo);
    CHECK(g.chain() == 4);
}

static void testRedAndWipe() {
    Game g = emptyGame();
    g.debugSetWipeY(12.0f);
    g.debugPlace(0, 0, 2);
    g.debugPlace(1, 0, 2);
    g.debugPlace(0, 1, 5);                  // sits on the combo, must fall after clear
    g.debugRecompute();
    int id = g.cell(0, 0).combo;
    CHECK(id >= 0);
    step(g, g.blueTime() + 0.05f);
    CHECK(g.cell(0, 0).state == DieState::Red);
    CHECK(g.cell(0, 0).occupied);           // wipe line has not reached row 0 yet
    std::vector<Event> all;
    for (int i = 0; i < 2000 && g.cell(1, 0).occupied; ++i) {
        step(g, 1.0f / 120.0f);
        auto ev = g.takeEvents();
        all.insert(all.end(), ev.begin(), ev.end());
    }
    CHECK(!g.cell(1, 0).occupied);
    CHECK(has(all, Ev::ComboScored));
    // 2 dice of value 2: 2 * 2*2 * 5 * multiplier(2) = 80
    CHECK(g.score() == 80);
    CHECK(g.cell(0, 0).occupied && g.cell(0, 0).value == 5);  // gravity
    CHECK(!g.cell(0, 1).occupied);
}

static void testDropCombo() {
    // No drop combo: after the 3s vanish the 6 lands, but no new group forms.
    Game h = emptyGame();
    h.debugSetWipeY(12.0f);
    h.debugPlace(0, 0, 3); h.debugPlace(1, 0, 3); h.debugPlace(2, 0, 3);
    h.debugPlace(1, 1, 6);
    h.debugRecompute();
    CHECK(h.chain() == 3);
    step(h, h.blueTime() + 6.5f);
    CHECK(h.score() > 0);
    CHECK(h.drops() == 0);
    CHECK(h.cell(1, 0).value == 6);

    // Drop combo: a 2 on top of the 3s falls next to a 2 already on the floor.
    Game f = emptyGame();
    f.debugSetWipeY(12.0f);
    f.debugPlace(0, 0, 3); f.debugPlace(1, 0, 3); f.debugPlace(2, 0, 3);
    f.debugPlace(2, 1, 2);                 // falls to (2,0)
    f.debugPlace(3, 0, 2);                 // diagonal now, adjacent after the drop
    f.debugRecompute();
    CHECK(f.cell(2, 1).combo < 0);
    step(f, f.blueTime() + 6.5f);
    CHECK(f.cell(2, 0).value == 2);
    CHECK(f.cell(2, 0).combo >= 0);
    CHECK(f.drops() == 2);
}

static void testWipeReset() {
    Game g = emptyGame();
    g.debugPlace(0, 0, 2);
    g.debugPlace(1, 0, 2);
    g.debugRecompute();
    CHECK(g.multiplier() == 2);
    // Let the combo turn red, get cleared, then a full empty sweep follows.
    step(g, 20.0f);
    CHECK(g.chain() == 0 && g.wipes() == 0);
    CHECK(g.multiplier() == 1);
}

static void testFallingAndPreview() {
    Game g(Config{}, Mode::Standard, 99);
    g.debugSetRiseTimer(1e9f, 0);
    auto pv = g.preview();
    CHECK(!pv.empty());
    int expectFront = pv.back().value;
    int expectRow = pv.back().row;
    CHECK(expectRow == 0);
    Input drop; drop.dropPressed = true;
    g.update(1.0f / 120.0f, drop);
    step(g, 1.0f);
    int c = g.config().width / 2;
    CHECK(g.cell(c, 0).occupied);
    CHECK(g.cell(c, 0).value == expectFront);
}

static void testMoveBlocked() {
    Game g(Config{}, Mode::Standard, 7);
    g.debugSetRiseTimer(1e9f, 0);
    int c0 = g.active().col;
    Input l; l.left = true; l.leftPressed = true;
    g.update(0.001f, l);
    CHECK(g.active().col == c0 - 1);
    // hold left: auto-repeat until the wall
    Input hold; hold.left = true;
    step(g, 1.0f, hold);
    CHECK(g.active().col == 0 || !g.active().present);
}

static void testRiseGameOver() {
    Game g = emptyGame();
    for (int r = 0; r < g.config().height; ++r) g.debugPlace(4, r, (r % 2) ? 1 : 6);
    g.debugSetRiseTimer(0.01f, 4);
    step(g, 0.1f);
    CHECK(g.gameOver());
}

static void testQuickRunTimeUp() {
    Config cfg; cfg.quickRunTime = 2.0f;
    Game g(cfg, Mode::QuickRun, 5);
    g.debugSetRiseTimer(1e9f, 0);
    step(g, 2.5f);
    CHECK(g.gameOver());
}

static void testSpeedLevels() {
    Config cfg;
    Game g1(cfg, Mode::Standard, 9);
    cfg.startLevel = 8;
    Game g8(cfg, Mode::Standard, 9);
    CHECK(g1.level() == 1 && g8.level() == 8);
    CHECK(g1.fallInterval() > 1.0f);                 // gentle start
    CHECK(g8.fallInterval() < g1.fallInterval() * 0.4f);
    CHECK(g8.riseInterval() < g1.riseInterval());
    CHECK(g1.riseCount() == 1 && g8.riseCount() == 2);
    CHECK(g8.nextRiseCols().size() == 2);
    CHECK(g8.nextRiseCols()[0] != g8.nextRiseCols()[1]);
    // two columns rise at once
    g8.debugRemoveActive();
    float t = g8.riseInterval() + 0.05f;
    std::vector<int> cols = g8.nextRiseCols();
    step(g8, t);
    for (int c : cols) CHECK(g8.cell(c, 0).occupied);
}

int main() {
    testSpeedLevels();
    testOrient();
    testComboForming();
    testRedAndWipe();
    testDropCombo();
    testWipeReset();
    testFallingAndPreview();
    testMoveBlocked();
    testRiseGameOver();
    testQuickRunTimeUp();
    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::printf("all core tests passed\n");
    return 0;
}
