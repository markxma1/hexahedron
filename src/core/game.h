// Hexahedron core rules. No rendering, no platform code, no allocations per frame
// beyond the event vector. Frontends (SDL3/GL, PS3, Godot, ...) drive it with
// update(dt, input) and read the public state + events.
//
// Rules (same idea as Hexaxis XXI, which this game is inspired by):
//  * Dice fall from the top and ROLL one face per cell they move (down/left/right).
//    The face pointing at the player (front) is the die's value.
//  * Groups of N+ connected dice showing N (N = 2..6) form a combo. Ones never combo.
//  * A new combo is blue. Adding a die resets its timer. When the timer runs out
//    it turns red. The wipe line sweeps top->bottom and removes red dice it passes.
//  * Random dice rise from the bottom (an arrow shows the column). Grid full = game over.
//  * Multiplier = chain + wipes + drops:
//      chain: +1 per die added to any combo
//      wipes: +1 each time the wipe line reaches the bottom while combos exist
//             (otherwise all three counters reset to 0)
//      drops: +1 per die of a combo formed by dice falling after a clear
#pragma once
#include "orient.h"
#include "rng.h"
#include <cstdint>
#include <vector>

namespace hx {

enum class Mode : uint8_t { Standard, Advanced, QuickRun };
enum class DieState : uint8_t { Normal, Blue, Red };

struct Config {
    int width = 10;
    int height = 12;
    float sweepTime = 6.0f;     // seconds for the wipe line to go top -> bottom
    float quickRunTime = 180.0f;
    int dicePerLevel = 25;
    int startLevel = 1;         // like Tetris: pick a harder start
};

struct Cell {
    bool occupied = false;
    Orient o = Orient::identity();
    uint8_t value = 0;          // front face
    DieState state = DieState::Normal;
    int combo = -1;
    uint32_t uid = 0;           // stable id for frontend animation
};

struct ActiveDie {
    bool present = false;
    int col = 0, row = 0;
    Orient o = Orient::identity();
    uint32_t uid = 0;
};

struct Combo {
    int id = -1;
    int value = 0;
    DieState state = DieState::Blue;
    float timer = 0;            // blue -> red countdown
    float timerMax = 1;
    int cleared = 0;            // dice removed so far
    bool alive = false;
};

struct Input {
    bool left = false, right = false, down = false, drop = false;  // held
    bool leftPressed = false, rightPressed = false, dropPressed = false; // edges
};

enum class Ev : uint8_t {
    Spawn, Roll, Land, ComboNew, ComboGrow, ComboRed, DieCleared, ComboScored,
    Gravity, Rise, WipeEnd, MultReset, LevelUp, GameOver, TimeUp, Blocked
};

struct Event {
    Ev type;
    uint32_t uid = 0;
    int col = 0, row = 0;
    int value = 0, count = 0;
    int64_t points = 0;
    int extra = 0;              // Roll: direction (Roll enum); WipeEnd: 1 if chain kept
};

struct PreviewStep { int row; int value; };

class Game {
public:
    Game(const Config& cfg, Mode mode, uint64_t seed);

    void update(float dt, const Input& in);
    std::vector<Event> takeEvents() { std::vector<Event> e; e.swap(events_); return e; }

    // ---- read-only state ----
    const Config& config() const { return cfg_; }
    Mode mode() const { return mode_; }
    const Cell& cell(int c, int r) const { return grid_[r * cfg_.width + c]; }
    const ActiveDie& active() const { return active_; }
    const std::vector<Combo>& combos() const { return combos_; }
    // Front faces the active die will show on each cell while falling straight down.
    std::vector<PreviewStep> preview() const;
    int landingRow() const;

    float wipeY() const { return wipeY_; }          // in cells, height..0
    float comboTimerFrac(int comboId) const;        // 1 = fresh, 0 = turning red
    int nextRiseCol() const { return riseCols_.empty() ? 0 : riseCols_[0]; }
    const std::vector<int>& nextRiseCols() const { return riseCols_; } // all columns of the next rise
    float riseFrac() const { return 1.0f - riseTimer_ / riseInterval(); } // 0..1
    float fallFrac() const;                         // progress to next fall step

    int64_t score() const { return score_; }
    int chain() const { return chain_; }
    int wipes() const { return wipes_; }
    int drops() const { return drops_; }
    int multiplier() const { int m = chain_ + wipes_ + drops_; return m < 1 ? 1 : m; }
    int bestMultiplier() const { return bestMult_; }
    int level() const { return level_; }
    int diceCleared() const { return diceCleared_; }
    float elapsed() const { return elapsed_; }
    float timeLeft() const { float t = cfg_.quickRunTime - elapsed_; return t < 0 ? 0 : t; }
    bool gameOver() const { return gameOver_; }

    // Speed curve (public so frontends can sync animations to it).
    float fallInterval() const;
    float currentStepInterval() const { return curInterval_; } // incl. soft/hard drop
    bool hardDropping() const { return hardDrop_; }
    float riseInterval() const;
    int riseCount() const;                          // dice per rise (grows with level)
    float blueTime() const;

    void setSweepTime(float t) { pendingSweep_ = t; }

    // ---- setup hooks (tests, tutorials, demo boards) ----
    void debugPlace(int c, int r, int frontValue);  // puts a die showing frontValue
    void debugRemoveActive() { active_.present = false; spawnTimer_ = 1e9f; }
    void debugAllowSpawn() { spawnTimer_ = 0; }
    void debugRecompute() { recomputeCombos(false); }
    void debugSetRiseTimer(float t, int col) { riseTimer_ = t; riseCols_.assign(1, col); }
    void debugSetWipeY(float y) { wipeY_ = y; }
    void debugSetActiveOrient(const Orient& o) { if (active_.present) active_.o = o; }
    const std::vector<Orient>& allOrients() const { return allOrients_; }

private:
    Cell& at(int c, int r) { return grid_[r * cfg_.width + c]; }
    bool inside(int c, int r) const { return c >= 0 && c < cfg_.width && r >= 0 && r < cfg_.height; }
    bool freeCell(int c, int r) const { return inside(c, r) && !cell(c, r).occupied; }
    Orient randomOrient();
    void emit(Ev t, uint32_t uid = 0, int c = 0, int r = 0, int v = 0, int n = 0, int64_t p = 0, int x = 0);

    void updateActive(float dt, const Input& in);
    bool tryRoll(Roll dir);
    void lockActive();
    void spawn();
    void updateCombos(float dt);
    void recomputeCombos(bool fromGravity);
    void clearRow(int r);
    bool applyGravity();
    void scoreFinishedCombos();
    void updateWipe(float dt);
    void updateRise(float dt);
    void rise();
    void riseColumn(int c);
    void pickRiseCols();
    void addCleared(int n);
    void endGame(bool timeUp);

    Config cfg_;
    Mode mode_;
    Rng rng_;
    std::vector<Cell> grid_;
    std::vector<Combo> combos_;
    std::vector<Orient> allOrients_;
    std::vector<Event> events_;
    ActiveDie active_;
    uint32_t nextUid_ = 1;

    float fallTimer_ = 0, lockTimer_ = 0, spawnTimer_ = 0, curInterval_ = 0.8f;
    bool landed_ = false, hardDrop_ = false;
    float dasTimer_ = 0; int dasDir_ = 0;

    float wipeY_ = 0, sweepTime_ = 6, pendingSweep_ = 0;
    bool comboThisSweep_ = false;
    float riseTimer_ = 0; std::vector<int> riseCols_;

    int64_t score_ = 0;
    int chain_ = 0, wipes_ = 0, drops_ = 0, bestMult_ = 1;
    int level_ = 1, diceCleared_ = 0;
    float elapsed_ = 0;
    bool gameOver_ = false;
};

} // namespace hx
