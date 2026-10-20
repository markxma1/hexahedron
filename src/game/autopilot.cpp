#include "autopilot.h"
#include <vector>

namespace hx {

float Autopilot::evaluate(const Game& g, int col, bool& reachable) {
    const ActiveDie& a = g.active();
    const Config& cfg = g.config();
    auto occupied = [&](int c, int r) { return c < 0 || c >= cfg.width || r < 0 || g.cell(c, r).occupied; };
    Orient o = a.o;
    int c = a.col, r = a.row;
    reachable = true;
    while (c != col) {
        int nc = c + (col > c ? 1 : -1);
        if (occupied(nc, r)) { reachable = false; return -1e9f; }
        o = o.rolled(col > c ? Roll::Right : Roll::Left);
        c = nc;
    }
    while (!occupied(c, r - 1)) { o = o.rolled(Roll::Down); --r; }
    int v = o.front();

    float score = 0;
    // Neighbours with the same number.
    const int dc[4] = {1, -1, 0, 0}, dr[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
        int nc = c + dc[k], nr = r + dr[k];
        if (nc < 0 || nc >= cfg.width || nr < 0 || nr >= cfg.height) continue;
        const Cell& n = g.cell(nc, nr);
        if (!n.occupied || n.value != v) continue;
        score += 4;
        if (n.state == DieState::Blue) score += 6;
    }
    // Size of the group the die would join.
    if (v >= 2) {
        std::vector<char> seen(cfg.width * cfg.height, 0);
        std::vector<int> st{r * cfg.width + c};
        seen[st[0]] = 1;
        int size = 0;
        while (!st.empty()) {
            int i = st.back(); st.pop_back(); ++size;
            int cc = i % cfg.width, rr = i / cfg.width;
            for (int k = 0; k < 4; ++k) {
                int nc = cc + dc[k], nr = rr + dr[k];
                if (nc < 0 || nc >= cfg.width || nr < 0 || nr >= cfg.height) continue;
                int j = nr * cfg.width + nc;
                if (seen[j] || !g.cell(nc, nr).occupied || g.cell(nc, nr).value != v) continue;
                seen[j] = 1; st.push_back(j);
            }
        }
        if (size >= v) score += 12 + v * 2;
        else score += size * 1.5f;
    } else {
        score -= 6;
    }
    score -= 0.8f * r;
    if (r >= cfg.height - 3) score -= 25;
    score -= 0.15f * (c > a.col ? c - a.col : a.col - c);
    score += rng_.unit() * (0.3f + (1.0f - skill) * 12.0f);
    return score;
}

int Autopilot::bestColumn(const Game& g) {
    int best = g.active().col;
    float bestScore = -1e30f;
    for (int c = 0; c < g.config().width; ++c) {
        bool ok;
        float s = evaluate(g, c, ok);
        if (ok && s > bestScore) { bestScore = s; best = c; }
    }
    return best;
}

Input Autopilot::think(const Game& g, float dt) {
    Input in;
    const ActiveDie& a = g.active();
    if (!a.present || g.gameOver()) { uid_ = 0; return in; }
    if (a.uid != uid_) {
        uid_ = a.uid;
        target_ = -1;
        replan_ = 0.15f + rng_.unit() * 0.2f; // "reaction time"
        dropDelay_ = 0.1f + rng_.unit() * 0.25f;
    }
    replan_ -= dt;
    pressCooldown_ -= dt;
    if (replan_ > 0) return in;
    if (target_ < 0 || replan_ < -0.25f) { target_ = bestColumn(g); replan_ = 0; }
    if (a.col != target_) {
        if (pressCooldown_ <= 0) {
            if (target_ < a.col) { in.left = in.leftPressed = true; }
            else { in.right = in.rightPressed = true; }
            pressCooldown_ = 0.07f;
        }
    } else {
        dropDelay_ -= dt;
        if (dropDelay_ <= 0) in.drop = in.dropPressed = true;
    }
    return in;
}

} // namespace hx
