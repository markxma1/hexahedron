#include "game.h"
#include <cmath>

namespace hx {

static const float kDasDelay = 0.17f;
static const float kDasRate = 0.07f;
static const float kSpawnDelay = 0.12f;
static const float kLockDelay = 0.35f;

Game::Game(const Config& cfg, Mode mode, uint64_t seed)
    : cfg_(cfg), mode_(mode), rng_(seed) {
    grid_.assign(cfg_.width * cfg_.height, Cell{});
    // All 24 orientations, reachable by rolling.
    allOrients_.push_back(Orient::identity());
    for (size_t i = 0; i < allOrients_.size(); ++i) {
        const Roll rolls[3] = {Roll::Down, Roll::Left, Roll::Right};
        for (Roll r : rolls) {
            Orient n = allOrients_[i].rolled(r);
            bool known = false;
            for (const Orient& k : allOrients_) if (k == n) { known = true; break; }
            if (!known) allOrients_.push_back(n);
        }
    }
    sweepTime_ = cfg_.sweepTime;
    wipeY_ = (float)cfg_.height;
    level_ = cfg_.startLevel < 1 ? 1 : cfg_.startLevel;
    curInterval_ = fallInterval();
    riseTimer_ = riseInterval();
    pickRiseCols();
    spawn();
}

Orient Game::randomOrient() { return allOrients_[rng_.range((int)allOrients_.size())]; }

void Game::emit(Ev t, uint32_t uid, int c, int r, int v, int n, int64_t p, int x) {
    Event e;
    e.type = t; e.uid = uid; e.col = c; e.row = r; e.value = v; e.count = n; e.points = p; e.extra = x;
    events_.push_back(e);
}

// Speed curves, Tetris style: level 1 is slow enough to learn the rolling,
// every level is noticeably faster and brings more rising dice.
// Gentle start: level 1 runs at half speed, normal speed from level 5 on.
static float earlySlowdown(int level) {
    return level >= 5 ? 1.0f : 1.0f + 0.25f * (float)(5 - level); // 2.0, 1.75, 1.5, 1.25
}

float Game::fallInterval() const {
    float t = 1.15f * std::pow(0.85f, (float)(level_ - 1)) * earlySlowdown(level_);
    return t < 0.07f ? 0.07f : t;
}
float Game::riseInterval() const {
    float t = 16.0f - 1.2f * (float)(level_ - 1);
    t = t < 2.5f ? 2.5f : t;
    return t * earlySlowdown(level_);
}
int Game::riseCount() const {
    int n = 1 + (level_ - 1) / 5;   // L1-5: 1, L6-10: 2, L11+: 3
    return n > 3 ? 3 : n;
}
float Game::blueTime() const {
    float t = 4.0f - 0.15f * (float)(level_ - 1);
    return t < 1.5f ? 1.5f : t;
}
float Game::fallFrac() const {
    float f = fallTimer_ / fallInterval();
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}
float Game::comboTimerFrac(int id) const {
    if (id < 0 || id >= (int)combos_.size()) return 0;
    const Combo& c = combos_[id];
    if (c.state == DieState::Red) return 0;
    return c.timerMax > 0 ? c.timer / c.timerMax : 0;
}

// ---------------------------------------------------------------- update

void Game::update(float dt, const Input& in) {
    if (gameOver_) return;
    elapsed_ += dt;
    if (mode_ == Mode::QuickRun && elapsed_ >= cfg_.quickRunTime) {
        elapsed_ = cfg_.quickRunTime;
        endGame(true);
        return;
    }
    updateActive(dt, in);
    if (gameOver_) return;
    updateCombos(dt);
    updateWipe(dt);
    updateRise(dt);
    if (multiplier() > bestMult_) bestMult_ = multiplier();
}

void Game::updateActive(float dt, const Input& in) {
    if (!active_.present) {
        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0) spawn();
        return;
    }

    // Horizontal movement with auto-repeat.
    int want = 0;
    if (in.leftPressed) want = -1;
    else if (in.rightPressed) want = 1;
    else if (in.left && !in.right) want = -1;
    else if (in.right && !in.left) want = 1;
    bool edge = in.leftPressed || in.rightPressed;
    if (want == 0) {
        dasDir_ = 0;
    } else if (want != dasDir_ || edge) {
        tryRoll(want < 0 ? Roll::Left : Roll::Right);
        dasDir_ = want;
        dasTimer_ = kDasDelay;
    } else {
        dasTimer_ -= dt;
        while (dasTimer_ <= 0) {
            tryRoll(want < 0 ? Roll::Left : Roll::Right);
            dasTimer_ += kDasRate;
        }
    }

    if (in.dropPressed) hardDrop_ = true;
    float interval = fallInterval();
    if (hardDrop_) interval = 0.02f;
    else if (in.down && interval > 0.05f) interval = 0.05f;
    curInterval_ = interval;

    fallTimer_ += dt;
    bool canFall = freeCell(active_.col, active_.row - 1);
    while (canFall && fallTimer_ >= interval) {
        fallTimer_ -= interval;
        tryRoll(Roll::Down);
        canFall = freeCell(active_.col, active_.row - 1);
    }
    if (canFall) {
        lockTimer_ = 0;
    } else {
        if (fallTimer_ > interval) fallTimer_ = interval;
        lockTimer_ += dt;
        float delay = hardDrop_ ? 0.0f : (in.down ? 0.12f : kLockDelay);
        if (lockTimer_ >= delay) lockActive();
    }
}

bool Game::tryRoll(Roll dir) {
    int c = active_.col, r = active_.row;
    if (dir == Roll::Down) r--;
    else if (dir == Roll::Left) c--;
    else c++;
    if (!freeCell(c, r)) {
        if (dir != Roll::Down) emit(Ev::Blocked, active_.uid, active_.col, active_.row);
        return false;
    }
    active_.col = c;
    active_.row = r;
    active_.o = active_.o.rolled(dir);
    emit(Ev::Roll, active_.uid, c, r, active_.o.front(), 0, 0, (int)dir);
    return true;
}

void Game::lockActive() {
    Cell& cl = at(active_.col, active_.row);
    cl.occupied = true;
    cl.o = active_.o;
    cl.value = (uint8_t)active_.o.front();
    cl.state = DieState::Normal;
    cl.combo = -1;
    cl.uid = active_.uid;
    emit(Ev::Land, cl.uid, active_.col, active_.row, cl.value, 0, 0, hardDrop_ ? 1 : 0);
    active_.present = false;
    spawnTimer_ = kSpawnDelay;
    hardDrop_ = false;
    fallTimer_ = 0;
    lockTimer_ = 0;
    recomputeCombos(false);
}

void Game::spawn() {
    int c = cfg_.width / 2, r = cfg_.height - 1;
    if (cell(c, r).occupied) { endGame(false); return; }
    active_.present = true;
    active_.col = c;
    active_.row = r;
    active_.o = randomOrient();
    active_.uid = nextUid_++;
    fallTimer_ = 0;
    lockTimer_ = 0;
    emit(Ev::Spawn, active_.uid, c, r, active_.o.front());
}

// ---------------------------------------------------------------- combos

void Game::updateCombos(float dt) {
    for (Combo& cb : combos_) {
        if (!cb.alive || cb.state != DieState::Blue) continue;
        cb.timer -= dt;
        if (cb.timer <= 0) {
            cb.timer = 0;
            cb.state = DieState::Red;
            int n = 0;
            for (Cell& cl : grid_)
                if (cl.occupied && cl.combo == cb.id) { cl.state = DieState::Red; ++n; }
            emit(Ev::ComboRed, 0, 0, 0, cb.value, n, 0, cb.id);
        }
    }
}

void Game::recomputeCombos(bool fromGravity) {
    const int W = cfg_.width, H = cfg_.height;
    std::vector<char> seen(W * H, 0);
    std::vector<int> comp, stack, ids;
    for (int start = 0; start < W * H; ++start) {
        const Cell& s = grid_[start];
        if (seen[start] || !s.occupied || s.value < 2) continue;
        comp.clear(); stack.clear(); ids.clear();
        stack.push_back(start);
        seen[start] = 1;
        while (!stack.empty()) {
            int i = stack.back(); stack.pop_back();
            comp.push_back(i);
            const Cell& ci = grid_[i];
            if (ci.combo >= 0) {
                bool known = false;
                for (int id : ids) if (id == ci.combo) known = true;
                if (!known) ids.push_back(ci.combo);
            }
            int c = i % W, r = i / W;
            const int dc[4] = {1, -1, 0, 0}, dr[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                int nc = c + dc[k], nr = r + dr[k];
                if (!inside(nc, nr)) continue;
                int j = nr * W + nc;
                if (seen[j] || !grid_[j].occupied || grid_[j].value != s.value) continue;
                seen[j] = 1;
                stack.push_back(j);
            }
        }

        int value = s.value, size = (int)comp.size();
        if (!ids.empty()) {
            // Grow / merge into the oldest combo of the group.
            int target = ids[0];
            for (int id : ids) if (id < target) target = id;
            Combo& t = combos_[target];
            for (int id : ids) {
                if (id == target) continue;
                if (combos_[id].state == DieState::Red) t.state = DieState::Red;
                t.cleared += combos_[id].cleared;
                combos_[id].alive = false;
            }
            int added = 0;
            for (int i : comp) {
                Cell& cl = grid_[i];
                if (cl.combo < 0) ++added;
                cl.combo = target;
                cl.state = t.state;
            }
            if (added > 0) {
                if (t.state == DieState::Blue) t.timer = t.timerMax = blueTime();
                if (fromGravity) drops_ += added; else chain_ += added;
                emit(Ev::ComboGrow, 0, 0, 0, value, added, 0, fromGravity ? 1 : 0);
            }
        } else if (size >= value) {
            Combo cb;
            cb.id = (int)combos_.size();
            cb.value = value;
            cb.state = DieState::Blue;
            cb.timer = cb.timerMax = blueTime();
            cb.alive = true;
            combos_.push_back(cb);
            for (int i : comp) {
                grid_[i].combo = cb.id;
                grid_[i].state = DieState::Blue;
            }
            if (fromGravity) drops_ += size; else chain_ += size;
            emit(Ev::ComboNew, 0, comp[0] % W, comp[0] / W, value, size, 0, fromGravity ? 1 : 0);
        }
    }
    if (multiplier() > bestMult_) bestMult_ = multiplier();
}

// ---------------------------------------------------------------- wipe line

void Game::clearRow(int r) {
    bool any = false;
    for (int c = 0; c < cfg_.width; ++c) {
        Cell& cl = at(c, r);
        if (!cl.occupied || cl.state != DieState::Red) continue;
        if (cl.combo >= 0) combos_[cl.combo].cleared++;
        emit(Ev::DieCleared, cl.uid, c, r, cl.value);
        cl = Cell{};
        any = true;
        addCleared(1);
    }
    if (!any) return;
    scoreFinishedCombos();
    if (applyGravity()) {
        emit(Ev::Gravity);
        recomputeCombos(true);
    }
}

void Game::scoreFinishedCombos() {
    std::vector<int> count(combos_.size(), 0);
    for (const Cell& cl : grid_)
        if (cl.occupied && cl.combo >= 0) count[cl.combo]++;
    for (Combo& cb : combos_) {
        if (!cb.alive || cb.state != DieState::Red || count[cb.id] > 0 || cb.cleared == 0) continue;
        cb.alive = false;
        int n = cb.cleared;
        int64_t pts = (int64_t)cb.value * n * n * 5 * multiplier();
        score_ += pts;
        comboThisSweep_ = true;
        emit(Ev::ComboScored, 0, 0, 0, cb.value, n, pts, multiplier());
    }
}

bool Game::applyGravity() {
    bool moved = false;
    for (int c = 0; c < cfg_.width; ++c) {
        int w = 0;
        for (int r = 0; r < cfg_.height; ++r) {
            if (!cell(c, r).occupied) continue;
            if (r != w) {
                at(c, w) = cell(c, r);
                at(c, r) = Cell{};
                moved = true;
            }
            ++w;
        }
    }
    return moved;
}

void Game::updateWipe(float dt) {
    const float H = (float)cfg_.height;
    float prev = wipeY_;
    wipeY_ -= dt * H / sweepTime_;
    for (int r = cfg_.height - 1; r >= 0; --r) {
        float center = r + 0.5f;
        if (prev > center && wipeY_ <= center) clearRow(r);
    }
    if (wipeY_ <= 0) {
        bool present = false;
        for (const Combo& cb : combos_) if (cb.alive) { present = true; break; }
        bool kept = present || comboThisSweep_;
        if (kept) {
            wipes_++;
        } else if (chain_ + wipes_ + drops_ > 0) {
            chain_ = wipes_ = drops_ = 0;
            emit(Ev::MultReset);
        }
        emit(Ev::WipeEnd, 0, 0, 0, 0, 0, 0, kept ? 1 : 0);
        comboThisSweep_ = false;
        wipeY_ += H;
        if (pendingSweep_ > 0) { sweepTime_ = pendingSweep_; pendingSweep_ = 0; }
    }
}

// ---------------------------------------------------------------- rising dice

void Game::updateRise(float dt) {
    riseTimer_ -= dt;
    if (riseTimer_ > 0) return;
    rise();
    riseTimer_ = riseInterval();
    pickRiseCols();
}

void Game::pickRiseCols() {
    riseCols_.clear();
    int n = riseCount();
    while ((int)riseCols_.size() < n && (int)riseCols_.size() < cfg_.width) {
        int c = rng_.range(cfg_.width);
        bool dup = false;
        for (int k : riseCols_) if (k == c) dup = true;
        if (!dup) riseCols_.push_back(c);
    }
}

void Game::rise() {
    for (int c : riseCols_) {
        riseColumn(c);
        if (gameOver_) return;
    }
}

void Game::riseColumn(int c) {
    int h = 0;
    while (h < cfg_.height && cell(c, h).occupied) ++h;
    if (h >= cfg_.height) { endGame(false); return; }
    for (int r = h; r > 0; --r) at(c, r) = cell(c, r - 1);
    Cell nc;
    nc.occupied = true;
    nc.o = randomOrient();
    nc.value = (uint8_t)nc.o.front();
    nc.uid = nextUid_++;
    at(c, 0) = nc;
    if (active_.present && active_.col == c && active_.row == h) {
        if (active_.row + 1 >= cfg_.height) { endGame(false); return; }
        active_.row++;
    }
    emit(Ev::Rise, nc.uid, c, 0, nc.value);
    recomputeCombos(false);
}

// ---------------------------------------------------------------- misc

void Game::addCleared(int n) {
    diceCleared_ += n;
    int lv = (cfg_.startLevel < 1 ? 1 : cfg_.startLevel) + diceCleared_ / cfg_.dicePerLevel;
    if (lv > level_) {
        level_ = lv;
        emit(Ev::LevelUp, 0, 0, 0, level_);
    }
}

void Game::endGame(bool timeUp) {
    if (gameOver_) return;
    gameOver_ = true;
    active_.present = false;
    if (timeUp) emit(Ev::TimeUp);
    emit(Ev::GameOver);
}

void Game::debugPlace(int c, int r, int frontValue) {
    if (!inside(c, r)) return;
    for (const Orient& o : allOrients_) {
        if (o.front() != frontValue) continue;
        Cell& cl = at(c, r);
        cl = Cell{};
        cl.occupied = true;
        cl.o = o;
        cl.value = (uint8_t)frontValue;
        cl.uid = nextUid_++;
        return;
    }
}

std::vector<PreviewStep> Game::preview() const {
    std::vector<PreviewStep> out;
    if (!active_.present) return out;
    Orient o = active_.o;
    for (int r = active_.row - 1; freeCell(active_.col, r); --r) {
        o = o.rolled(Roll::Down);
        out.push_back({r, o.front()});
    }
    return out;
}

int Game::landingRow() const {
    int r = active_.row;
    while (freeCell(active_.col, r - 1)) --r;
    return r;
}

} // namespace hx
