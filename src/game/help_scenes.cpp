// Animated "How to play": every help page plays a short scripted scene with the
// real rules engine, so what you see is exactly how the game behaves.
#include "app.h"
#include <cstdio>

namespace hx {

namespace {
const float kStep = 1.0f / 120.0f;
enum CueKind { CuePlace, CueRecompute, CueRise, CueWipeY };
Color8 col(const Rgb& c, float a = 1) { return rgba(c.r, c.g, c.b, a); }
} // namespace

void App::swapWorld(World& w) {
    std::swap(game_, w.game);
    std::swap(vis_, w.vis);
    std::swap(parts_, w.parts);
    std::swap(frags_, w.frags);
    std::swap(popups_, w.popups);
    std::swap(banners_, w.banners);
    std::swap(rolled_, w.rolled);
    std::swap(simAccum_, w.simAccum);
    std::swap(displayScore_, w.displayScore);
    std::swap(gameOverTimer_, w.gameOverTimer);
    std::swap(clearSeq_, w.clearSeq);
    std::swap(demo_, w.demo);
    std::swap(hardDropping_, w.hardDropping);
    std::swap(mode_, w.mode);
    std::swap(spawnedUid_, w.spawnedUid);
}

void App::enterHelp(Screen ret) {
    helpReturn_ = ret;
    helpPage_ = 0;
    screen_ = Screen::Help;
    if (!helpWorld_) {
        swapWorld(helpStash_);   // park the game (demo or paused) that was on screen
        helpWorld_ = true;
    }
    buildHelpScene(helpPage_);
}

void App::leaveHelp() {
    screen_ = helpReturn_;
    if (helpWorld_) {
        swapWorld(helpStash_);
        helpStash_ = World();
        helpWorld_ = false;
    }
}

// Script for one help page: board setup, the dice the "player" moves, timed cues.
void App::buildHelpScene(int page) {
    Config cfg;
    int level = 1;
    plan_.clear();
    cues_.clear();
    cueIdx_ = 0;
    planSt_ = PlanState();
    sceneT_ = 0;
    sceneLen_ = 1e9f;
    wipeHold_ = 1e9f;     // wipe line parked at the top unless a scene releases it
    scenePilot_ = false;
    sceneInfo_ = 0;
    bool rise = false;
    auto place = [&](int c, int r, int v) { cues_.push_back(Cue{0, CuePlace, c, r, v}); };

    switch (page) {
    case 0:  // dice stacking game: the computer plays
    case 12:
    case 13:
        scenePilot_ = true; rise = true; wipeHold_ = 0;
        break;
    case 1:  // object: the grid fills up from both sides
        scenePilot_ = true; rise = true; wipeHold_ = 0; level = 10;
        { const int v[10] = {2, 5, 1, 3, 6, 4, 1, 5, 2, 6}; for (int c = 0; c < 10; ++c) place(c, 0, v[c]); }
        break;
    case 2:  // the die rolls as it moves; make three 3s
        place(3, 0, 3); place(4, 0, 3);
        plan_.push_back(PlanStep{2, 3, 0.6f, 0.5f});
        sceneLen_ = 6.5f;
        break;
    case 3:  // blue combo turns red
        place(6, 0, 4); place(7, 0, 4); place(8, 0, 4);
        plan_.push_back(PlanStep{9, 4, 0.4f, 0.3f});
        sceneLen_ = 8.0f;
        break;
    case 4:  // the wipe line clears red dice
        level = 12;
        for (int c = 2; c <= 6; ++c) place(c, 0, 5);
        wipeHold_ = 2.8f;
        sceneLen_ = 10.0f;
        sceneInfo_ = 1;
        break;
    case 5:  // preview line
        level = 3;
        plan_.push_back(PlanStep{3, 0, 1.2f, -1});
        place(3, 0, 2); place(3, 1, 6);
        sceneLen_ = 9.5f;
        break;
    case 6:  // arrow for the next random die
        place(1, 0, 2); place(2, 0, 5); place(6, 0, 1); place(7, 0, 3); place(7, 1, 6);
        cues_.push_back(Cue{0.01f, CueRise, 25, 7, 0});
        cues_.push_back(Cue{3.5f, CueRise, 25, 2, 0});
        sceneLen_ = 7.5f;
        break;
    case 7:  // big combos are worth more
        level = 12;
        for (int c = 1; c <= 7; ++c) place(c, 0, 6);
        place(1, 1, 6);
        wipeHold_ = 2.8f;
        sceneLen_ = 10.0f;
        sceneInfo_ = 1;
        break;
    case 8:  // multipliers
    case 9:  // combo chain: every die added counts
        place(1, 0, 3); place(2, 0, 3);
        plan_.push_back(PlanStep{3, 3, 0.4f, 0.3f});
        plan_.push_back(PlanStep{4, 3, 0.3f, 0.3f});
        plan_.push_back(PlanStep{0, 3, 0.3f, 0.3f});
        sceneLen_ = 9.0f;
        sceneInfo_ = 2;
        break;
    case 10: // wipe chain
        place(4, 0, 2); place(5, 0, 2);
        cues_.push_back(Cue{0.01f, CueWipeY, 40, 0, 0});
        wipeHold_ = 0;
        sceneLen_ = 14.0f;
        sceneInfo_ = 2;
        break;
    case 11: // drop combo
        level = 12;
        for (int c = 2; c <= 6; ++c) place(c, 0, 5);
        place(1, 0, 2);
        place(2, 1, 2); place(3, 1, 1); place(4, 1, 6);
        wipeHold_ = 2.8f;
        sceneLen_ = 10.0f;
        sceneInfo_ = 2;
        break;
    }
    cues_.push_back(Cue{0, CueRecompute, 0, 0, 0});

    cfg.startLevel = level;
    mode_ = Mode::Standard;
    demo_ = true;
    game_.reset(new Game(cfg, Mode::Standard, 0x4E15u + (uint64_t)page * 977u));
    vis_.clear(); parts_.clear(); frags_.clear(); popups_.clear(); banners_.clear(); rolled_.clear();
    simAccum_ = 0; displayScore_ = 0; gameOverTimer_ = 0; clearSeq_ = 0;
    hardDropping_ = false; spawnedUid_ = 0;
    pilot_ = Autopilot(0x77u + (uint64_t)page);
    pilot_.skill = 0.85f;
    sceneRise_ = rise || page == 6;
    if (!rise) game_->debugSetRiseTimer(1e9f, 0);
    if (!scenePilot_ && plan_.empty()) game_->debugRemoveActive();
    // apply the t=0 cues right away so the board is ready on the first frame
    while (cueIdx_ < cues_.size() && cues_[cueIdx_].t <= 0) {
        const Cue& c = cues_[cueIdx_++];
        if (c.kind == CuePlace) game_->debugPlace(c.a, c.b, c.c);
        else if (c.kind == CueRecompute) game_->debugRecompute();
    }
    game_->takeEvents();
    if (wipeHold_ > 0) game_->debugSetWipeY((float)cfg.height);
}

// Plays a scripted move: wait, roll sideways to the target column, then drop.
Input App::drivePlan(const Game& g, const std::vector<PlanStep>& plan, PlanState& st, float dt) {
    Input in;
    const ActiveDie& a = g.active();
    if (!a.present) return in;
    if (a.uid != st.uid) {
        st.uid = a.uid;
        st.cur = st.next++;
        st.t = st.moveT = st.atColT = 0;
        st.dropped = false;
    }
    if (st.cur < 0 || st.cur >= (int)plan.size()) return in;
    const PlanStep& p = plan[st.cur];
    st.t += dt;
    if (st.t < p.wait) return in;
    if (p.col >= 0 && a.col != p.col) {
        st.moveT -= dt;
        if (st.moveT <= 0) {
            if (a.col > p.col) { in.left = true; in.leftPressed = true; }
            else { in.right = true; in.rightPressed = true; }
            st.moveT = 0.28f;
        }
        return in;
    }
    if (p.dropDelay < 0) return in;
    st.atColT += dt;
    if (st.atColT >= p.dropDelay) {
        in.drop = true;
        if (!st.dropped) { in.dropPressed = true; st.dropped = true; }
    }
    return in;
}

void App::updateHelpScene(float dt) {
    simAccum_ += dt;
    while (simAccum_ >= kStep) {
        simAccum_ -= kStep;
        sceneT_ += kStep;
        if (sceneT_ > sceneLen_ || (game_->gameOver() && (gameOverTimer_ += kStep) > 2.5f)) {
            buildHelpScene(helpPage_);
            return;
        }
        if (game_->gameOver()) continue;

        while (cueIdx_ < cues_.size() && cues_[cueIdx_].t <= sceneT_) {
            const Cue& c = cues_[cueIdx_++];
            switch (c.kind) {
            case CuePlace: game_->debugPlace(c.a, c.b, c.c); break;
            case CueRecompute: game_->debugRecompute(); break;
            case CueRise: game_->debugSetRiseTimer(c.a / 10.0f, c.b); break;
            case CueWipeY: game_->debugSetWipeY(c.a / 10.0f); break;
            }
        }
        if (sceneT_ < wipeHold_) game_->debugSetWipeY((float)game_->config().height);

        Input in;
        if (scenePilot_) {
            in = pilot_.think(*game_, kStep);
        } else {
            const ActiveDie& a = game_->active();
            if (a.present && a.uid != planSt_.uid) {
                int idx = planSt_.next;
                if (idx >= (int)plan_.size()) {
                    game_->debugRemoveActive();     // script done: no more dice
                    continue;
                }
                // Pick the starting orientation that ends with the wanted face:
                // try all 24 on a copy of the game with the same scripted moves.
                if (plan_[idx].front > 0) {
                    for (const Orient& o : game_->allOrients()) {
                        Game sim = *game_;
                        sim.debugSetActiveOrient(o);
                        PlanState ps = planSt_;
                        uint32_t uid = a.uid;
                        for (int i = 0; i < 120 * 20 && sim.active().present && sim.active().uid == uid; ++i) {
                            sim.update(kStep, drivePlan(sim, plan_, ps, kStep));
                            sim.takeEvents();
                        }
                        bool ok = false;
                        for (int r = 0; r < sim.config().height && !ok; ++r)
                            for (int c = 0; c < sim.config().width; ++c)
                                if (sim.cell(c, r).occupied && sim.cell(c, r).uid == uid) { ok = sim.cell(c, r).value == plan_[idx].front; break; }
                        if (ok) { game_->debugSetActiveOrient(o); snapActiveVisual(); break; }
                    }
                }
            }
            in = drivePlan(*game_, plan_, planSt_, kStep);
        }
        stepGame(kStep, in);
    }
}

// Live numbers next to the scene (score or the multiplier breakdown).
void App::drawHelpInfo(Batch& b, float x, float y, float s) {
    if (!game_ || sceneInfo_ == 0) return;
    const Theme& th = theme(theme_);
    char buf[96];
    if (sceneInfo_ == 1) {
        std::snprintf(buf, sizeof buf, "SCORE  %lld", (long long)game_->score());
        fontBody_.drawShadowed(b, buf, x, y, 40 * s, col(th.accent), Align::Center, 3 * s);
    } else {
        std::snprintf(buf, sizeof buf, "MULTIPLIER  x%d", game_->multiplier());
        fontBody_.drawShadowed(b, buf, x, y, 40 * s, col(th.accent), Align::Center, 3 * s);
        std::snprintf(buf, sizeof buf, "chain %d   +   wipes %d   +   drops %d", game_->chain(), game_->wipes(), game_->drops());
        fontBody_.drawShadowed(b, buf, x, y + 52 * s, 28 * s, rgba(0.85f, 0.9f, 1), Align::Center, 2 * s);
    }
}

} // namespace hx
