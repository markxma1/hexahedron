#include "app.h"
#include "log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

// Name and credits (title screen, footer, last help page).
#define HX_GAME_NAME "HEXAHEDRON"
#ifndef HX_REMAKE_AUTHOR
#define HX_REMAKE_AUTHOR "Xma1"
#endif
#ifndef HX_REMAKE_YEAR
#define HX_REMAKE_YEAR "2026"
#endif

namespace hx {

namespace {

const float kSimStep = 1.0f / 120.0f;
const int kBgW = 1280, kBgH = 720;

std::string fmtNum(int64_t v) {
    std::string s = std::to_string(v < 0 ? -v : v), out;
    int n = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        out.insert(out.begin(), s[i]);
        if (++n % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return v < 0 ? "-" + out : out;
}

std::string fmtTime(float sec) {
    int t = (int)sec;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", t / 60, t % 60);
    return buf;
}

Color8 col(const Rgb& c, float a = 1, float k = 1) { return rgba(c.r * k, c.g * k, c.b * k, a); }

Quat quatOf(const Orient& o) {
    float r[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i][j] = o.m[i][j];
    return Quat::fromRows(r);
}

const char* modeName(Mode m) {
    switch (m) {
    case Mode::Standard: return "STANDARD";
    case Mode::Advanced: return "ADVANCED";
    case Mode::QuickRun: return "QUICK RUN";
    }
    return "";
}

const int kMaxStartLevel = 15;

struct HelpPage { const char* title; const char* body; };

// How-to-play pages. Each one has a matching live scene in help_scenes.cpp.
const HelpPage kHelp[] = {
    {"WELCOME", "Dice drop into the well from above.\nStack them cleverly and clear them\nbefore the well overflows."},
    {"THE GOAL", "Survive as long as you can.\nYour own dice fill the well from the top,\nrandom dice push in from the bottom.\nOnce a column is full, the run is over."},
    {"MATCHING (1 OF 3)", "Group dice that show the same number.\nA group needs at least as many dice as its\nnumber: two 2s, three 3s ... six 6s.\nEvery move ROLLS the die by one face -\nonly the face looking at you counts."},
    {"MATCHING (2 OF 3)", "A fresh group glows blue.\nKeep feeding it to keep it blue.\nLeave it alone too long and it turns red."},
    {"MATCHING (3 OF 3)", "The sweep line travels down the well.\nEvery red die it touches bursts\nand the group pays out its points."},
    {"HINTS (1 OF 2)", "In Standard mode a column of small faces\nruns below your die: it tells you which\nnumber will face you on every row\nif you let it fall straight down."},
    {"HINTS (2 OF 2)", "A small arrow under the well marks the\ncolumn where the next random die will\npush in. Plan around it!"},
    {"BIG POINTS (1 OF 2)", "High numbers and big groups pay best.\nPoints = number x dice x dice x 5,\nso eight 6s beat two 2s by a mile."},
    {"BIG POINTS (2 OF 2)", "Everything you score is multiplied by\nyour multiplier. Three things raise it:\nchains, sweeps and drops."},
    {"MULTIPLIER: CHAIN", "Each die that joins a group adds +1.\nA group of three 3s is already x3."},
    {"MULTIPLIER: SWEEP", "When the sweep line reaches the bottom\nwhile a group is on the board: +1.\nIf the board has no group at that moment,\nthe multiplier drops back to the start."},
    {"MULTIPLIER: DROP", "When dice fall down into a gap after a\nclear and form a new group on landing,\nevery die of that group adds +1.\nSet these up for huge multipliers!"},
    {"CONTROLS", "LEFT / RIGHT  roll the die sideways\nDOWN  roll down faster\nUP / SPACE / (A)  drop\nESC / START  pause      F1 / SELECT  help\nQ / E / wheel / LB / RB  zoom\nmouse drag / Z C / right stick  look around\nX / middle click  reset view      F11  fullscreen"},
    {"THANK YOU!", "for playing " HX_GAME_NAME ".\n\nMade by " HX_REMAKE_AUTHOR ", " HX_REMAKE_YEAR ", with the help of AI.\nInspired by Hexaxis XXI (PSP, 2007)\nby Tim Hackett / Darksoft."},
};
const int kHelpCount = (int)(sizeof(kHelp) / sizeof(kHelp[0]));

} // namespace

// ====================================================================== setup

App::~App() {
#ifndef HX_NO_THREADS
    if (bgThread_.joinable()) bgThread_.join();
#endif
}

bool App::init(Platform* p, const LaunchOptions& opt) {
    plat_ = p;
    gfx_ = p->renderer;
    opt_ = opt;

    std::vector<uint8_t> ttf;
    if (!p->files->readAsset("fonts/Orbitron.ttf", ttf) || !fontTitle_.load(gfx_, ttf, 96)) {
        logMsg("hexahedron: cannot load fonts/Orbitron.ttf");
        return false;
    }
    if (!p->files->readAsset("fonts/Exo2.ttf", ttf) || !fontBody_.load(gfx_, ttf, 72)) {
        logMsg("hexahedron: cannot load fonts/Exo2.ttf");
        return false;
    }

    Image dice = makeDiceAtlas(256);
    diceTex_ = gfx_->createTexture(dice.w, dice.h, dice.px.data(), true, false);
    Image spr = makeSpriteAtlas();
    spriteTex_ = gfx_->createTexture(spr.w, spr.h, spr.px.data(), true, false);
    Image grid = makeGridCell(128);
    gridTex_ = gfx_->createTexture(grid.w, grid.h, grid.px.data(), true, true);

    std::vector<MeshVertex> mv;
    std::vector<uint16_t> mi;
    buildRoundedCube(0.14f, 5, mv, mi);
    diceMesh_ = gfx_->createMesh(mv.data(), (int)mv.size(), mi.data(), (int)mi.size());

    loadSettings();
    loadScores();
    if (opt.theme >= 0) settings_.themeMode = opt.theme % kThemeCount;
    theme_ = prevTheme_ = settings_.themeMode >= 0 ? settings_.themeMode : 0;

    // First background now, the rest in the background.
    bgImages_.resize(kThemeCount);
    for (int i = 0; i < kThemeCount; ++i) bgReady_[i] = 0;
    bgImages_[theme_] = makeBackground(theme_, kBgW, kBgH);
    bgReady_[theme_] = 1;
    uploadBackgrounds();
#ifndef HX_NO_THREADS
    int first = theme_;
    bgThread_ = std::thread([this, first]() {
        for (int i = 0; i < kThemeCount; ++i) {
            if (i == first) continue;
            bgImages_[i] = makeBackground(i, kBgW, kBgH);
            bgReady_[i] = 1;
        }
    });
#endif

    synth_.init(p->audio ? p->audio->sampleRate() : 48000);
    synth_.setTheme(theme_);
    applyVolumes();
    if (settings_.fullscreen != p->fullscreen) p->requestFullscreenToggle = true;

    if (opt.autoplay || opt.skipTitle) {
        startGame(opt.mode, false);
        screen_ = Screen::Playing;
    } else {
        startGame(Mode::Standard, true);
        screen_ = Screen::Title;
    }
    return true;
}

void App::uploadBackgrounds() {
    for (int i = 0; i < kThemeCount; ++i) {
        if (bgReady_[i] == 1) {
            Image& im = bgImages_[i];
            bgTex_[i] = gfx_->createTexture(im.w, im.h, im.px.data(), true, false);
            im.px.clear();
            im.px.shrink_to_fit();
            bgReady_[i] = 2;
        }
    }
#ifdef HX_NO_THREADS
    // No threads: generate one missing background per call.
    for (int i = 0; i < kThemeCount; ++i)
        if (bgReady_[i] == 0) { bgImages_[i] = makeBackground(i, kBgW, kBgH); bgReady_[i] = 1; break; }
#endif
}

void App::applyVolumes() {
    synth_.setMusicVolume(settings_.musicVol / 10.0f * 0.7f);
    synth_.setSfxVolume(settings_.sfxVol / 10.0f);
}

// ====================================================================== persistence

void App::loadSettings() {
    std::string txt;
    if (!plat_->files->loadText("settings.txt", txt)) return;
    std::istringstream in(txt);
    std::string key;
    int v;
    while (in >> key >> v) {
        if (key == "music") settings_.musicVol = std::max(0, std::min(10, v));
        else if (key == "sound") settings_.sfxVol = std::max(0, std::min(10, v));
        else if (key == "fullscreen") settings_.fullscreen = v != 0;
        else if (key == "startlevel") settings_.startLevel = std::max(1, std::min(kMaxStartLevel, v));
        else if (key == "theme") settings_.themeMode = (v >= -1 && v < kThemeCount) ? v : -1;
    }
}

void App::saveSettings() {
    std::ostringstream o;
    o << "music " << settings_.musicVol << "\nsound " << settings_.sfxVol << "\nfullscreen "
      << (plat_->fullscreen ? 1 : 0) << "\ntheme " << settings_.themeMode
      << "\nstartlevel " << settings_.startLevel << "\n";
    plat_->files->saveText("settings.txt", o.str());
}

void App::loadScores() {
    std::string txt;
    if (!plat_->files->loadText("scores.txt", txt)) return;
    std::istringstream in(txt);
    int m;
    ScoreEntry e;
    long long sc;
    while (in >> m >> sc >> e.level >> e.mult >> e.seconds) {
        if (m < 0 || m > 2) continue;
        e.score = sc;
        scores_[m].push_back(e);
    }
    for (auto& list : scores_) {
        std::sort(list.begin(), list.end(), [](const ScoreEntry& a, const ScoreEntry& b) { return a.score > b.score; });
        if (list.size() > 10) list.resize(10);
    }
}

void App::saveScores() {
    std::ostringstream o;
    for (int m = 0; m < 3; ++m)
        for (const ScoreEntry& e : scores_[m])
            o << m << " " << (long long)e.score << " " << e.level << " " << e.mult << " " << e.seconds << "\n";
    plat_->files->saveText("scores.txt", o.str());
}

int App::insertScore(Mode m, const ScoreEntry& e) {
    auto& list = scores_[(int)m];
    int pos = 0;
    while (pos < (int)list.size() && list[pos].score >= e.score) ++pos;
    if (pos >= 10) return -1;
    list.insert(list.begin() + pos, e);
    if (list.size() > 10) list.resize(10);
    saveScores();
    return pos;
}

// ====================================================================== flow

void App::startGame(Mode m, bool demo) {
    mode_ = m;
    demo_ = demo;
    Config cfg;
    if (!demo) cfg.startLevel = std::max(1, std::min(kMaxStartLevel, settings_.startLevel));
    uint64_t seed = demo ? 0xD3A0u + (uint64_t)(time_ * 1000) : (uint64_t)std::rand() * 2654435761u + (uint64_t)(time_ * 1e6);
    game_.reset(new Game(cfg, m, seed));
    vis_.clear();
    parts_.clear();
    frags_.clear();
    popups_.clear();
    banners_.clear();
    displayScore_ = 0;
    simAccum_ = 0;
    gameOverTimer_ = 0;
    clearSeq_ = 0;
    pilot_ = Autopilot(seed ^ 0x5151);
    pilot_.skill = demo ? 0.85f : 1.0f;
    int t = settings_.themeMode >= 0 ? settings_.themeMode : (cfg.startLevel - 1) % kThemeCount;
    if (demo && settings_.themeMode < 0) t = theme_;
    setTheme(t);
    if (!demo) banner(modeName(m), "GET READY", theme(theme_).accent, 1.6f);
}

void App::setTheme(int t) {
    t = (t % kThemeCount + kThemeCount) % kThemeCount;
    if (t != theme_) {
        prevTheme_ = theme_;
        themeFade_ = 0;
        bgParts_.clear();
    }
    theme_ = t;
    synth_.setTheme(t);
    if (game_) game_->setSweepTime(60.0f / synth_.bpm() * 8.0f);
}

void App::frame(float dt, const InputState& in) {
    if (dt > 0.1f) dt = 0.1f;
    time_ += dt;
    frameDt_ = dt;
    screenTime_ += dt;
    in_ = in;
    bool inject[ActCount] = {};
    if (in.tap) handleTap(inject);
    for (int a = 0; a < ActCount; ++a) {
        pressed_[a] = in.held[a] && !prevHeld_[a];
        repeat_[a] = false;
        if (pressed_[a]) { repeat_[a] = true; repeatTimer_[a] = 0.35f; }
        else if (in.held[a]) {
            repeatTimer_[a] -= dt;
            if (repeatTimer_[a] <= 0) { repeat_[a] = true; repeatTimer_[a] = 0.08f; }
        }
        prevHeld_[a] = in.held[a];
        if (inject[a]) pressed_[a] = repeat_[a] = true;
    }

    uploadBackgrounds();
    if (pendingTheme_ >= 0) { setTheme(pendingTheme_); pendingTheme_ = -1; }
    themeFade_ = std::min(1.0f, themeFade_ + dt * 0.8f);

    // Global zoom (original: L/R triggers).
    if (in.held[ActZoomIn]) zoom_ = std::max(0.6f, zoom_ - dt * 0.6f);
    if (in.held[ActZoomOut]) zoom_ = std::min(1.5f, zoom_ + dt * 0.6f);
    if (in.zoomSteps != 0) zoom_ = clampf(zoom_ * std::pow(0.93f, in.zoomSteps), 0.6f, 1.5f);

    // Free look: turn the board a little to the side / up and down.
    lookYaw_ -= in.lookDX * 1.6f + in.lookRateX * dt * 0.8f;
    lookPitch_ += in.lookDY * 1.2f + in.lookRateY * dt * 0.6f;
    lookYaw_ = clampf(lookYaw_, -0.55f, 0.55f);
    lookPitch_ = clampf(lookPitch_, -0.22f, 0.4f);
    if (in.lookReset) lookResetAnim_ = true;
    if (in.lookDX != 0 || in.lookDY != 0 || in.lookRateX != 0 || in.lookRateY != 0) lookResetAnim_ = false;
    if (lookResetAnim_) {
        lookYaw_ = approach(lookYaw_, 0, 6, dt);
        lookPitch_ = approach(lookPitch_, 0, 6, dt);
    }

    Screen before = screen_;
    switch (screen_) {
    case Screen::Title: updateTitle(dt); break;
    case Screen::LevelSelect: updateLevelSelect(dt); break;
    case Screen::Playing: updatePlaying(dt); break;
    case Screen::Paused: updatePaused(); break;
    case Screen::Help: updateHelp(dt); break;
    case Screen::Scores: updateScores(); break;
    case Screen::Options: updateOptions(); break;
    case Screen::GameOver: updateGameOver(dt); break;
    }
    if (screen_ != before) screenTime_ = 0;

    bool frozen = screen_ == Screen::Paused;
    synth_.setMusicDuck(frozen ? 0.35f : 1.0f);
    if (!frozen) {
        updateVisuals(dt);
    }
    updateBgParticles(dt);

    render();
    pumpAudio();
}

void App::stepGame(float dt, const Input& in) {
    game_->update(dt, in);
    for (const Event& e : game_->takeEvents()) handleEvent(e);
}

void App::snapActiveVisual() {
    const ActiveDie& a = game_->active();
    auto it = vis_.find(a.uid);
    if (it == vis_.end()) return;
    it->second.rot = it->second.toRot = quatOf(a.o);
    it->second.value = a.o.front();
}

void App::runDemo(float dt) {
    // Attract mode: the computer plays behind the menu.
    simAccum_ += dt;
    while (simAccum_ >= kSimStep) {
        simAccum_ -= kSimStep;
        if (game_->gameOver()) {
            gameOverTimer_ += kSimStep;
            if (gameOverTimer_ > 2.5f) { startGame(Mode::Standard, true); break; }
            continue;
        }
        stepGame(kSimStep, pilot_.think(*game_, kSimStep));
    }
}

void App::updateLevelSelect(float dt) {
    runDemo(dt);
    int& lv = settings_.startLevel;
    if (repeat(ActLeft) || repeat(ActDown)) { lv = lv > 1 ? lv - 1 : kMaxStartLevel; synth_.play(Sfx::MenuMove); }
    if (repeat(ActRight) || repeat(ActUp)) { lv = lv < kMaxStartLevel ? lv + 1 : 1; synth_.play(Sfx::MenuMove); }
    if (pressed(ActConfirm) || pressed(ActDrop)) {
        synth_.play(Sfx::MenuSelect);
        saveSettings();
        startGame(pendingMode_, false);
        screen_ = Screen::Playing;
    }
    if (pressed(ActBack) || pressed(ActPause)) { screen_ = Screen::Title; synth_.play(Sfx::MenuBack); }
}

void App::updateTitle(float dt) {
    runDemo(dt);

    const int nItems = 7;
    if (repeat(ActUp)) { menuSel_ = (menuSel_ + nItems - 1) % nItems; synth_.play(Sfx::MenuMove); }
    if (repeat(ActDown)) { menuSel_ = (menuSel_ + 1) % nItems; synth_.play(Sfx::MenuMove); }
    if (pressed(ActHelp)) { enterHelp(Screen::Title); synth_.play(Sfx::MenuSelect); }
    if (pressed(ActConfirm) || pressed(ActDrop)) {
        synth_.play(Sfx::MenuSelect);
        switch (menuSel_) {
        case 0: pendingMode_ = Mode::Standard; screen_ = Screen::LevelSelect; break;
        case 1: pendingMode_ = Mode::Advanced; screen_ = Screen::LevelSelect; break;
        case 2: pendingMode_ = Mode::QuickRun; screen_ = Screen::LevelSelect; break;
        case 3: scoreTab_ = 0; scoreHighlight_ = -1; screen_ = Screen::Scores; break;
        case 4: enterHelp(Screen::Title); break;
        case 5: optSel_ = 0; screen_ = Screen::Options; break;
        case 6: quit_ = true; break;
        }
    }
}

void App::updatePlaying(float dt) {
    if (pressed(ActPause) || (pressed(ActBack) && !opt_.autoplay)) {
        pauseSel_ = 0;
        screen_ = Screen::Paused;
        synth_.play(Sfx::Pause);
        return;
    }
    if (pressed(ActHelp)) {
        enterHelp(Screen::Paused);
        synth_.play(Sfx::Pause);
        return;
    }
    if (pressed(ActPrevTheme)) setTheme(theme_ - 1);
    if (pressed(ActNextTheme)) setTheme(theme_ + 1);

    Input ci;
    ci.left = in_.held[ActLeft];
    ci.right = in_.held[ActRight];
    ci.down = in_.held[ActDown];
    ci.drop = in_.held[ActDrop] || in_.held[ActUp];
    ci.leftPressed = pressed(ActLeft);
    ci.rightPressed = pressed(ActRight);
    ci.dropPressed = pressed(ActDrop) || pressed(ActUp);

    simAccum_ += dt;
    bool first = true;
    while (simAccum_ >= kSimStep) {
        simAccum_ -= kSimStep;
        Input step = opt_.autoplay ? pilot_.think(*game_, kSimStep) : ci;
        if (!first) step.leftPressed = step.rightPressed = step.dropPressed = false;
        first = false;
        stepGame(kSimStep, step);
        if (game_->gameOver()) break;
    }
    if (game_->gameOver()) {
        gameOverTimer_ += dt;
        if (gameOverTimer_ > 1.4f) {
            if (opt_.autoplay) { startGame(mode_, false); return; }
            ScoreEntry e;
            e.score = game_->score();
            e.level = game_->level();
            e.mult = game_->bestMultiplier();
            e.seconds = (int)game_->elapsed();
            lastRank_ = e.score > 0 ? insertScore(mode_, e) : -1;
            screen_ = Screen::GameOver;
        }
    }
}

void App::updatePaused() {
    const int n = 5;
    if (repeat(ActUp)) { pauseSel_ = (pauseSel_ + n - 1) % n; synth_.play(Sfx::MenuMove); }
    if (repeat(ActDown)) { pauseSel_ = (pauseSel_ + 1) % n; synth_.play(Sfx::MenuMove); }
    if (pauseSel_ == 2 && (repeat(ActLeft) || repeat(ActRight))) {
        int t = theme_ + (repeat(ActLeft) ? -1 : 1);
        setTheme(t);
        if (settings_.themeMode >= 0) { settings_.themeMode = theme_; saveSettings(); }
        synth_.play(Sfx::MenuMove);
    }
    if (pressed(ActPause) || pressed(ActBack)) { screen_ = Screen::Playing; synth_.play(Sfx::MenuBack); return; }
    if (pressed(ActConfirm) || pressed(ActDrop)) {
        synth_.play(Sfx::MenuSelect);
        switch (pauseSel_) {
        case 0: screen_ = Screen::Playing; break;
        case 1: startGame(mode_, false); screen_ = Screen::Playing; break;
        case 2: setTheme(theme_ + 1); break;
        case 3: enterHelp(Screen::Paused); break;
        case 4: startGame(Mode::Standard, true); screen_ = Screen::Title; break;
        }
    }
}

void App::updateHelp(float dt) {
    int page = helpPage_;
    if (repeat(ActLeft) || pressed(ActPrevTheme)) { if (helpPage_ > 0) { --helpPage_; synth_.play(Sfx::MenuMove); } }
    if (repeat(ActRight) || pressed(ActNextTheme) || pressed(ActConfirm)) {
        if (helpPage_ < kHelpCount - 1) { ++helpPage_; synth_.play(Sfx::MenuMove); }
        else if (pressed(ActConfirm)) { leaveHelp(); synth_.play(Sfx::MenuBack); return; }
    }
    if (pressed(ActBack) || pressed(ActHelp) || pressed(ActPause)) { leaveHelp(); synth_.play(Sfx::MenuBack); return; }
    if (helpPage_ != page) buildHelpScene(helpPage_);
    updateHelpScene(dt);
}

void App::updateScores() {
    if (repeat(ActLeft)) { scoreTab_ = (scoreTab_ + 2) % 3; scoreHighlight_ = -1; synth_.play(Sfx::MenuMove); }
    if (repeat(ActRight)) { scoreTab_ = (scoreTab_ + 1) % 3; scoreHighlight_ = -1; synth_.play(Sfx::MenuMove); }
    if (pressed(ActBack) || pressed(ActConfirm) || pressed(ActPause)) {
        synth_.play(Sfx::MenuBack);
        if (demo_) screen_ = Screen::Title;
        else { startGame(Mode::Standard, true); screen_ = Screen::Title; }
    }
}

void App::updateOptions() {
    const int n = 5;
    if (repeat(ActUp)) { optSel_ = (optSel_ + n - 1) % n; synth_.play(Sfx::MenuMove); }
    if (repeat(ActDown)) { optSel_ = (optSel_ + 1) % n; synth_.play(Sfx::MenuMove); }
    int d = repeat(ActLeft) ? -1 : (repeat(ActRight) ? 1 : 0);
    bool confirm = pressed(ActConfirm) || pressed(ActDrop);
    if (optSel_ == 4 && confirm) d = 1;
    if (d != 0 || confirm) {
        switch (optSel_) {
        case 0: settings_.musicVol = std::max(0, std::min(10, settings_.musicVol + (d ? d : 1))); if (!d && settings_.musicVol == 10) settings_.musicVol = 0; break;
        case 1: settings_.sfxVol = std::max(0, std::min(10, settings_.sfxVol + (d ? d : 1))); if (!d && settings_.sfxVol == 10) settings_.sfxVol = 0; break;
        case 2: if (plat_->hasWindow) plat_->requestFullscreenToggle = true; break;
        case 3: {
            int m = settings_.themeMode + (d ? d : 1);
            if (m < -1) m = kThemeCount - 1;
            if (m >= kThemeCount) m = -1;
            settings_.themeMode = m;
            setTheme(m >= 0 ? m : 0);
            break;
        }
        case 4: screen_ = Screen::Title; break;
        }
        applyVolumes();
        saveSettings();
        synth_.play(optSel_ == 1 ? Sfx::ComboGrow : Sfx::MenuMove, 3);
    }
    if (pressed(ActBack) || pressed(ActPause)) { screen_ = Screen::Title; saveSettings(); synth_.play(Sfx::MenuBack); }
}

void App::updateGameOver(float dt) {
    (void)dt;
    if (screenTime_ > 0.6f && (pressed(ActConfirm) || pressed(ActDrop) || pressed(ActBack))) {
        synth_.play(Sfx::MenuSelect);
        scoreTab_ = (int)mode_;
        scoreHighlight_ = lastRank_;
        startGame(Mode::Standard, true);
        screen_ = Screen::Scores;
    }
}

// ====================================================================== events -> effects

void App::handleEvent(const Event& e) {
    const Theme& th = theme(theme_);
    bool loud = !demo_;
    auto S = [&](Sfx s, float p = 0) { if (loud) synth_.play(s, p); };
    switch (e.type) {
    case Ev::Spawn: {
        VisDie v;
        v.pos = cellCenter(e.col, e.row);
        v.rot = quatOf(game_->active().o);
        v.spawn = 0;
        v.value = e.value;
        v.toPos = v.pos; v.toRot = v.rot;
        vis_[e.uid] = v;
        ring(Vec3(e.col + 0.5f, e.row + 0.5f, 0.6f), th.accent, 0.7f, 1.0f);
        hardDropping_ = false;
        break;
    }
    case Ev::Roll: {
        bool side = e.extra != (int)Roll::Down;
        float dur = side ? 0.09f : clampf(game_->currentStepInterval() * 0.9f, 0.015f, 0.16f);
        if (game_->hardDropping()) dur = 0.025f;
        bool found = false;
        for (RollMark& m : rolled_)
            if (m.uid == e.uid) { m.side = m.side || side; m.dur = std::min(m.dur, dur); found = true; }
        if (!found) rolled_.push_back({e.uid, side, dur});
        if (side) S(Sfx::Move, e.extra == (int)Roll::Left ? -1.f : 1.f);
        break;
    }
    case Ev::Blocked:
        if (blockedCooldown_ <= 0) { S(Sfx::Blocked); blockedCooldown_ = 0.15f; }
        break;
    case Ev::Land: {
        bool hard = e.extra != 0;
        S(hard ? Sfx::HardLand : Sfx::Land);
        dust(Vec3(e.col + 0.5f, (float)e.row, 0.6f), hard ? 14 : 6);
        if (hard) shake_ = std::max(shake_, 0.35f);
        auto it = vis_.find(e.uid);
        if (it != vis_.end()) it->second.bounce = hard ? 1.0f : 0.6f;
        hardDropping_ = false;
        break;
    }
    case Ev::ComboNew:
        S(Sfx::ComboNew, (float)e.value);
        ring(cellCenter(e.col, e.row), Rgb{0.3f, 0.7f, 1.0f}, 1.6f, 2.5f);
        popup(cellCenter(e.col, e.row) + Vec3(0, 0.6f, 0.5f), (e.extra ? "DROP +" : "CHAIN +") + std::to_string(e.count), Rgb{0.5f, 0.85f, 1.0f}, 0.8f);
        multPulse_ = 1;
        break;
    case Ev::ComboGrow:
        S(Sfx::ComboGrow, (float)std::min(game_->chain(), 15));
        multPulse_ = 1;
        break;
    case Ev::ComboRed:
        S(Sfx::ComboRed);
        break;
    case Ev::DieCleared: {
        Vec3 p = dieVisualPos(e.uid, e.col, e.row);
        lastClearPos_ = p;
        burstDie(p, e.value, false);
        S(Sfx::Clear, (float)clearSeq_++);
        break;
    }
    case Ev::ComboScored: {
        S(Sfx::Scored);
        float big = std::min(1.0f, e.count / 10.0f);
        shake_ = std::max(shake_, 0.2f + 0.5f * big);
        flash_ = std::max(flash_, 0.15f + 0.3f * big);
        Rgb c = diceColor(e.value);
        popup(lastClearPos_ + Vec3(0, 0.3f, 0.6f), "+" + fmtNum(e.points), Rgb{1, 1, 1}, 1.1f + big * 0.6f);
        popup(lastClearPos_ + Vec3(0, -0.4f, 0.6f), std::to_string(e.count) + " x " + std::to_string(e.value) + "   x" + std::to_string(e.extra), c, 0.7f);
        if (e.count >= 8) banner("HUGE COMBO!", fmtNum(e.points) + " POINTS", c, 1.6f);
        break;
    }
    case Ev::Gravity:
        break;
    case Ev::Rise: {
        S(Sfx::Rise);
        VisDie v;
        v.pos = Vec3(e.col + 0.5f, -0.6f, 0.5f);
        v.rot = quatOf(game_->cell(e.col, 0).o);
        v.toPos = v.pos; v.toRot = v.rot;
        v.value = e.value;
        vis_[e.uid] = v;
        dust(Vec3(e.col + 0.5f, 0.0f, 0.7f), 10);
        shake_ = std::max(shake_, 0.15f);
        break;
    }
    case Ev::WipeEnd:
        clearSeq_ = 0;
        if (e.extra) {
            S(Sfx::WipeKept);
            popup(Vec3(game_->config().width * 0.5f, -0.8f, 1.2f), "WIPE CHAIN x" + std::to_string(game_->wipes()), th.wipe, 0.9f);
            multPulse_ = 1;
        }
        break;
    case Ev::MultReset:
        S(Sfx::MultReset);
        popup(Vec3(game_->config().width * 0.5f, -0.8f, 1.2f), "MULTIPLIER RESET", Rgb{1.0f, 0.3f, 0.25f}, 0.9f);
        break;
    case Ev::LevelUp:
        S(Sfx::LevelUp);
        if (!demo_) banner("LEVEL " + std::to_string(e.value), "", th.accent, 1.8f);
        if (settings_.themeMode < 0 && !demo_) pendingTheme_ = (e.value - 1) % kThemeCount;
        flash_ = std::max(flash_, 0.35f);
        break;
    case Ev::TimeUp:
        if (!demo_) banner("TIME UP!", "", Rgb{1, 0.85f, 0.3f}, 2.5f);
        break;
    case Ev::GameOver:
        S(Sfx::GameOver);
        if (!demo_ && mode_ != Mode::QuickRun) banner("GAME OVER", "", Rgb{1, 0.25f, 0.2f}, 2.5f);
        shake_ = std::max(shake_, 0.6f);
        break;
    }
}

Vec3 App::dieVisualPos(uint32_t uid, int c, int r) const {
    auto it = vis_.find(uid);
    return it != vis_.end() ? it->second.pos : cellCenter(c, r);
}

void App::burstDie(const Vec3& p, int value, bool big) {
    Rgb c = diceColor(value);
    for (int i = 0; i < 6; ++i) {
        Fragment f;
        f.p = p + Vec3(fxRng_.unit() - 0.5f, fxRng_.unit() - 0.5f, fxRng_.unit() - 0.5f) * 0.5f;
        f.v = Vec3((fxRng_.unit() - 0.5f) * 6, fxRng_.unit() * 5 + 1, fxRng_.unit() * 5 + 2);
        f.axis = normalize(Vec3(fxRng_.unit() - 0.5f, fxRng_.unit() - 0.5f, fxRng_.unit() - 0.5f));
        f.spin = (fxRng_.unit() + 0.5f) * 12;
        f.maxLife = 0.9f + fxRng_.unit() * 0.5f;
        f.size = 0.22f + fxRng_.unit() * 0.12f;
        f.value = value;
        frags_.push_back(f);
    }
    int n = big ? 30 : 16;
    for (int i = 0; i < n; ++i) {
        Particle q;
        q.p = p;
        float a = fxRng_.unit() * 2 * kPi, sp = 2 + fxRng_.unit() * 6;
        q.v = Vec3(std::cos(a) * sp, std::sin(a) * sp, fxRng_.unit() * 3);
        q.maxLife = 0.4f + fxRng_.unit() * 0.5f;
        q.size0 = 0.25f + fxRng_.unit() * 0.25f;
        q.size1 = 0;
        q.drag = 3;
        q.spr = SprSparkle;
        q.r = c.r * 0.6f + 0.4f; q.g = c.g * 0.6f + 0.4f; q.b = c.b * 0.6f + 0.4f;
        q.inten = 3.0f;
        q.rotV = (fxRng_.unit() - 0.5f) * 6;
        parts_.push_back(q);
    }
    ring(p, c, 1.8f, 3.0f);
    Particle core;
    core.p = p + Vec3(0, 0, 0.3f);
    core.maxLife = 0.25f;
    core.size0 = 1.4f; core.size1 = 0.2f;
    core.spr = SprDot;
    core.inten = 4;
    core.r = c.r * 0.5f + 0.5f; core.g = c.g * 0.5f + 0.5f; core.b = c.b * 0.5f + 0.5f;
    parts_.push_back(core);
}

void App::dust(const Vec3& p, int n) {
    for (int i = 0; i < n; ++i) {
        Particle q;
        q.p = p + Vec3((fxRng_.unit() - 0.5f) * 0.8f, 0, (fxRng_.unit() - 0.5f) * 0.4f);
        q.v = Vec3((fxRng_.unit() - 0.5f) * 3, fxRng_.unit() * 1.2f, fxRng_.unit() * 1.5f);
        q.maxLife = 0.5f + fxRng_.unit() * 0.4f;
        q.size0 = 0.25f; q.size1 = 0.7f;
        q.drag = 4;
        q.spr = SprSmoke;
        Rgb ac = theme(theme_).accent;
        q.r = ac.r; q.g = ac.g; q.b = ac.b;
        q.a = 0.18f;
        q.inten = 0.8f;
        q.rotV = (fxRng_.unit() - 0.5f) * 2;
        parts_.push_back(q);
    }
}

void App::ring(const Vec3& p, Rgb c, float size, float inten) {
    Particle q;
    q.p = p;
    q.maxLife = 0.45f;
    q.size0 = size * 0.3f;
    q.size1 = size;
    q.spr = SprRing;
    q.r = c.r; q.g = c.g; q.b = c.b;
    q.inten = inten;
    parts_.push_back(q);
}

void App::popup(const Vec3& p, const std::string& t, Rgb c, float size) {
    Popup pp;
    pp.world = p;
    pp.text = t;
    pp.color = c;
    pp.size = size;
    popups_.push_back(pp);
}

void App::banner(const std::string& t, const std::string& sub, Rgb c, float life) {
    Banner b;
    b.text = t;
    b.sub = sub;
    b.color = c;
    b.maxLife = life;
    banners_.clear();
    banners_.push_back(b);
}

// ====================================================================== visual update

void App::updateVisuals(float dt) {
    blockedCooldown_ -= dt;
    shake_ = approach(shake_, 0, 5, dt);
    flash_ = approach(flash_, 0, 6, dt);
    multPulse_ = approach(multPulse_, 0, 5, dt);
    displayScore_ = approach(displayScore_, (float)game_->score(), 8, dt);
    if (std::fabs(displayScore_ - (float)game_->score()) < 1) displayScore_ = (float)game_->score();

    for (auto& kv : vis_) kv.second.seen = false;
    const Config& cfg = game_->config();
    auto startRoll = [&](uint32_t uid, VisDie& v, const Vec3& tgt, const Quat& trot) {
        for (const RollMark& m : rolled_) {
            if (m.uid != uid) continue;
            v.fromPos = v.pos;
            v.fromRot = v.rot;
            v.toPos = tgt;
            v.toRot = trot;
            v.animT = 0;
            v.animDur = m.dur;
            v.animBump = m.side ? 0.3f : 0.16f;
            v.vy = 0;
            return true;
        }
        return false;
    };
    for (int r = 0; r < cfg.height; ++r)
        for (int c = 0; c < cfg.width; ++c) {
            const Cell& cl = game_->cell(c, r);
            if (!cl.occupied) continue;
            auto it = vis_.find(cl.uid);
            if (it == vis_.end()) {
                VisDie v;
                v.pos = cellCenter(c, r);
                v.rot = quatOf(cl.o);
                v.toPos = v.pos; v.toRot = v.rot;
                it = vis_.emplace(cl.uid, v).first;
            }
            VisDie& v = it->second;
            v.seen = true;
            v.value = cl.value;
            Vec3 tgt = cellCenter(c, r);
            Quat trot = quatOf(cl.o);
            if (startRoll(cl.uid, v, tgt, trot)) {
            } else if (v.animT < 1) {
                v.toPos = tgt;
                v.toRot = trot;
            } else {
                if (tgt.y < v.pos.y - 1e-3f) { // falling after a clear
                    v.vy -= 50 * dt;
                    v.pos.y += v.vy * dt;
                    if (v.pos.y <= tgt.y) {
                        v.pos.y = tgt.y;
                        if (v.vy < -3) v.bounce = 0.7f;
                        v.vy = 0;
                    }
                } else if (tgt.y > v.pos.y + 1e-3f) { // pushed up by a rising die
                    v.pos.y = approach(v.pos.y, tgt.y, 14, dt);
                    v.vy = 0;
                } else {
                    v.pos.y = tgt.y;
                }
                v.pos.x = approach(v.pos.x, tgt.x, 20, dt);
                v.pos.z = approach(v.pos.z, tgt.z, 20, dt);
                v.rot = trot;
            }
        }

    const ActiveDie& a = game_->active();
    if (a.present) {
        auto it = vis_.find(a.uid);
        if (it == vis_.end()) {
            VisDie v;
            v.pos = cellCenter(a.col, a.row);
            v.rot = quatOf(a.o);
            v.toPos = v.pos; v.toRot = v.rot;
            v.spawn = 0;
            it = vis_.emplace(a.uid, v).first;
        }
        VisDie& v = it->second;
        v.seen = true;
        v.value = a.o.front();
        Vec3 tgt = cellCenter(a.col, a.row);
        Quat trot = quatOf(a.o);
        if (startRoll(a.uid, v, tgt, trot)) {
        } else if (v.animT >= 1) {
            // pushed up by a rising die
            v.pos = lerp(v.pos, tgt, 1 - std::exp(-20 * dt));
            v.rot = trot;
        } else {
            v.toPos = tgt;
            v.toRot = trot;
        }
    }
    rolled_.clear();

    for (auto it = vis_.begin(); it != vis_.end();) {
        VisDie& v = it->second;
        if (!v.seen) { it = vis_.erase(it); continue; }
        if (v.animT < 1) {
            v.animT = std::min(1.0f, v.animT + dt / v.animDur);
            float e = smooth(v.animT);
            v.pos = lerp(v.fromPos, v.toPos, e) + Vec3(0, 0, v.animBump * std::sin(kPi * v.animT));
            v.rot = slerp(v.fromRot, v.toRot, e);
            if (v.animT >= 1) { v.pos = v.toPos; v.rot = v.toRot; }
        }
        v.spawn = std::min(1.0f, v.spawn + dt * 4);
        v.bounce = approach(v.bounce, 0, 7, dt);
        ++it;
    }

    for (Particle& p : parts_) {
        p.life += dt;
        p.v.y -= p.grav * dt;
        p.v = p.v * std::exp(-p.drag * dt);
        p.p += p.v * dt;
        p.rot += p.rotV * dt;
    }
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.life >= p.maxLife; }), parts_.end());
    for (Fragment& f : frags_) {
        f.life += dt;
        f.v.y -= 14 * dt;
        f.p += f.v * dt;
        f.q = Quat::axisAngle(f.axis, f.spin * f.life);
    }
    frags_.erase(std::remove_if(frags_.begin(), frags_.end(), [](const Fragment& f) { return f.life >= f.maxLife; }), frags_.end());
    for (Popup& p : popups_) p.life += dt;
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.life >= p.maxLife; }), popups_.end());
    for (Banner& b : banners_) b.life += dt;
    banners_.erase(std::remove_if(banners_.begin(), banners_.end(), [](const Banner& b) { return b.life >= b.maxLife; }), banners_.end());

    // Danger: how close is the stack to the top?
    int maxH = 0;
    for (int c = 0; c < cfg.width; ++c) {
        int h = 0;
        while (h < cfg.height && game_->cell(c, h).occupied) ++h;
        maxH = std::max(maxH, h);
    }
    riseWarn_ = approach(riseWarn_, clampf((maxH - (cfg.height - 4)) / 3.0f, 0, 1), 4, dt);
}

void App::updateBgParticles(float dt) {
    const Theme& th = theme(theme_);
    float rate = 0;
    switch (th.fx) {
    case BgFx::Stars: rate = 6; break;
    case BgFx::Spiral: rate = 25; break;
    case BgFx::Fireflies: rate = 4; break;
    case BgFx::Cards: rate = 3; break;
    case BgFx::Bubbles: rate = 5; break;
    case BgFx::Chrome: rate = 4; break;
    case BgFx::Coins: rate = 3; break;
    }
    bgSpawn_ += rate * dt;
    while (bgSpawn_ >= 1 && bgParts_.size() < 220) {
        bgSpawn_ -= 1;
        BgParticle p;
        p.x = fxRng_.unit();
        p.y = fxRng_.unit();
        p.phase = fxRng_.unit() * 6.28f;
        switch (th.fx) {
        case BgFx::Stars:
            if (fxRng_.unit() < 0.06f) { // shooting star
                p.spr = SprStreak; p.x = fxRng_.unit() * 0.8f; p.y = fxRng_.unit() * 0.4f;
                p.vx = 0.9f; p.vy = 0.35f; p.size = 90; p.maxLife = 0.8f; p.rot = std::atan2(0.35f, 0.9f);
                p.r = 0.8f; p.g = 0.9f; p.b = 1;
            } else {
                p.spr = SprSparkle; p.size = 8 + fxRng_.unit() * 18; p.maxLife = 1.5f + fxRng_.unit() * 2.5f;
                p.r = 0.8f; p.g = 0.85f; p.b = 1;
            }
            break;
        case BgFx::Spiral: {
            float ang = fxRng_.unit() * 6.28f;
            p.x = 0.5f + std::cos(ang) * 0.75f; p.y = 0.5f + std::sin(ang) * 0.75f;
            p.spr = SprDot; p.size = 5 + fxRng_.unit() * 10; p.maxLife = 4 + fxRng_.unit() * 2;
            bool warm = fxRng_.unit() < 0.3f;
            p.r = warm ? 1.0f : 0.3f; p.g = warm ? 0.6f : 1.0f; p.b = warm ? 0.2f : 0.9f;
            break;
        }
        case BgFx::Fireflies:
            p.spr = SprDot; p.size = 8 + fxRng_.unit() * 10; p.maxLife = 4 + fxRng_.unit() * 4;
            p.y = 0.5f + fxRng_.unit() * 0.5f;
            p.r = 0.9f; p.g = 1.0f; p.b = 0.5f;
            break;
        case BgFx::Cards: {
            int s = fxRng_.range(4);
            p.spr = SprSpade + s; p.size = 22 + fxRng_.unit() * 26; p.maxLife = 7;
            p.x = fxRng_.unit(); p.y = 1.1f; p.vy = -0.06f - fxRng_.unit() * 0.05f;
            p.rotV = (fxRng_.unit() - 0.5f) * 2;
            bool red = (SprSpade + s == SprHeart || SprSpade + s == SprDiamond);
            p.r = red ? 0.9f : 0.95f; p.g = red ? 0.2f : 0.95f; p.b = red ? 0.2f : 0.9f;
            p.a = 0.5f;
            break;
        }
        case BgFx::Bubbles: {
            p.spr = SprBubble; p.size = 14 + fxRng_.unit() * 40; p.maxLife = 9;
            p.y = 1.1f; p.vy = -0.05f - fxRng_.unit() * 0.06f;
            Rgb c = Rgb{0.5f + 0.5f * std::sin(p.phase), 0.5f + 0.5f * std::sin(p.phase + 2.1f), 0.5f + 0.5f * std::sin(p.phase + 4.2f)};
            p.r = c.r; p.g = c.g; p.b = c.b;
            p.a = 0.7f;
            break;
        }
        case BgFx::Chrome:
            p.spr = fxRng_.unit() < 0.7f ? SprBubble : SprSparkle; p.size = 10 + fxRng_.unit() * 30; p.maxLife = 8;
            p.y = 1.1f; p.vy = -0.03f - fxRng_.unit() * 0.03f;
            p.r = 0.8f; p.g = 0.95f; p.b = 1.0f; p.a = 0.6f;
            break;
        case BgFx::Coins:
            p.spr = fxRng_.unit() < 0.75f ? SprCoin : SprSparkle; p.size = 18 + fxRng_.unit() * 22; p.maxLife = 9;
            p.y = -0.1f; p.vy = 0.05f + fxRng_.unit() * 0.05f; p.rotV = 2 + fxRng_.unit() * 3;
            p.r = 1.0f; p.g = 0.78f; p.b = 0.3f;
            break;
        }
        bgParts_.push_back(p);
    }
    for (BgParticle& p : bgParts_) {
        p.life += dt;
        if (th.fx == BgFx::Spiral) {
            float dx = p.x - 0.5f, dy = p.y - 0.5f;
            float r = std::sqrt(dx * dx + dy * dy) + 1e-3f;
            p.vx = (-dy / r * 0.12f - dx / r * 0.06f);
            p.vy = (dx / r * 0.12f - dy / r * 0.06f);
            if (r < 0.03f) p.life = p.maxLife;
        } else if (th.fx == BgFx::Fireflies) {
            p.vx = 0.02f * std::sin(time_ * 0.7f + p.phase * 3);
            p.vy = 0.015f * std::cos(time_ * 0.9f + p.phase * 5);
        } else if (th.fx == BgFx::Cards) {
            p.vx = 0.05f * std::sin(time_ * 0.8f + p.phase);
        } else if (th.fx == BgFx::Bubbles || th.fx == BgFx::Chrome) {
            p.vx = 0.01f * std::sin(time_ * 1.3f + p.phase);
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rot += p.rotV * dt;
    }
    bgParts_.erase(std::remove_if(bgParts_.begin(), bgParts_.end(), [](const BgParticle& p) { return p.life >= p.maxLife; }), bgParts_.end());
}

// ====================================================================== rendering

void App::computeCamera(int w, int h) {
    viewW_ = w;
    viewH_ = h;
    const Config& cfg = game_->config();
    float W = (float)cfg.width, H = (float)cfg.height;
    float aspect = (float)w / (float)h;
    float fov = 34.0f * kPi / 180.0f;
    float t = std::tan(fov * 0.5f);
    float dist = (H * 0.5f + 1.7f) / t;
    float distW = (W * 0.5f + 1.2f) / (t * aspect * 0.6f);
    dist = std::max(dist, distW) * zoom_ * (screen_ == Screen::Help ? 1.12f : 1.0f);

    float wantShift = (screen_ == Screen::Title || screen_ == Screen::Options || screen_ == Screen::LevelSelect || screen_ == Screen::Help) ? 1.0f : 0.0f;
    titleShift_ = approach(titleShift_, wantShift, 4, frameDt_);
    float titleShift = titleShift_;

    // Slight parallax: the camera leans toward the falling die's column.
    float follow = 0;
    if (screen_ == Screen::Playing && game_->active().present)
        follow = ((float)game_->active().col + 0.5f - W * 0.5f) / W;
    followYaw_ = approach(followYaw_, follow * 0.12f, 3, frameDt_);
    float yaw = 0.16f + 0.035f * std::sin(time_ * 0.21f) + followYaw_ + lookYaw_;
    float pitch = 0.12f + 0.02f * std::sin(time_ * 0.17f) + lookPitch_;
    Vec3 target(W * 0.5f - titleShift * W * 0.55f * std::min(aspect / 1.78f, 1.4f), H * 0.5f - 0.25f, 0.5f);
    Vec3 eye = target + Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)) * dist;
    if (shake_ > 0.001f) {
        Vec3 off(std::sin(time_ * 63.0f) * shake_ * 0.18f, std::cos(time_ * 57.0f) * shake_ * 0.18f, 0);
        eye += off;
        target += off * 0.5f;
    }
    view_ = Mat4::lookAt(eye, target, Vec3(0, 1, 0));
    proj_ = Mat4::perspective(fov, aspect, 0.5f, 300.0f);
    camPos_ = eye;
    camRight_ = Vec3(view_.m[0], view_.m[4], view_.m[8]);
    camUp_ = Vec3(view_.m[1], view_.m[5], view_.m[9]);
}

bool App::project(const Vec3& p, float& sx, float& sy) const {
    Vec4 c = (proj_ * view_) * v4(p.x, p.y, p.z, 1);
    if (c.w <= 0.01f) return false;
    sx = (c.x / c.w * 0.5f + 0.5f) * viewW_;
    sy = (1 - (c.y / c.w * 0.5f + 0.5f)) * viewH_;
    return true;
}

void App::sprite(Batch& b, int spr, float x0, float y0, float x1, float y1, Color8 c) {
    float u0, v0, u1, v1;
    spriteUV(spr, u0, v0, u1, v1);
    b.rect(x0, y0, x1, y1, u0, v0, u1, v1, c);
}

void App::render() {
    int w, h;
    gfx_->viewportSize(w, h);
    if (w <= 0 || h <= 0) return;
    computeCamera(w, h);
    const Theme& th = theme(theme_);

    FrameParams fp;
    std::copy(view_.m, view_.m + 16, fp.view);
    std::copy(proj_.m, proj_.m + 16, fp.proj);
    fp.camPos[0] = camPos_.x; fp.camPos[1] = camPos_.y; fp.camPos[2] = camPos_.z;
    Vec3 L = normalize(Vec3(-0.45f, 0.75f, 0.65f));
    fp.lightDir[0] = L.x; fp.lightDir[1] = L.y; fp.lightDir[2] = L.z;
    fp.lightColor[0] = 1.05f; fp.lightColor[1] = 1.0f; fp.lightColor[2] = 0.95f;
    fp.skyColor[0] = th.sky.r; fp.skyColor[1] = th.sky.g; fp.skyColor[2] = th.sky.b;
    fp.groundColor[0] = th.ground.r; fp.groundColor[1] = th.ground.g; fp.groundColor[2] = th.ground.b;
    fp.clearColor[0] = fp.clearColor[1] = fp.clearColor[2] = 0;
    fp.bloomStrength = th.bloom;
    fp.bloomThreshold = 1.0f;
    fp.exposure = 1.0f + flash_ * 0.8f;
    fp.vignette = 0.4f;
    gfx_->beginFrame(fp);

    Batch b(gfx_);
    drawBackground(b, w, h);
    drawBoard(b);
    b.flush();
    drawDice();
    drawWorldFx(b);
    b.flush();

    gfx_->beginOverlay();
    if (screen_ == Screen::Playing || screen_ == Screen::Paused || screen_ == Screen::GameOver)
        drawHud(b, w, h);
    drawMenus(b, w, h);
    drawTouchButtons(b, w, h, h / 1080.0f);
    b.flush();
    gfx_->endFrame();
}

void App::drawBackground(Batch& b, int w, int h) {
    auto layer = [&](int t, float alpha) {
        if (alpha <= 0.001f) return;
        TexId tex = bgTex_[t] ? bgTex_[t] : bgTex_[theme_];
        if (!tex) return;
        const Theme& th = theme(t);
        // cover-fit with a slow drift
        float sa = (float)w / h, ta = (float)kBgW / kBgH;
        float uw = 1, vh = 1;
        if (sa > ta) vh = ta / sa; else uw = sa / ta;
        float z = 0.94f;
        uw *= z; vh *= z;
        float cx = 0.5f + 0.02f * std::sin(time_ * 0.05f), cy = 0.5f + 0.015f * std::cos(time_ * 0.04f);
        b.begin(tex, Blend::Alpha, false);
        b.rect(0, 0, (float)w, (float)h, cx - uw / 2, cy - vh / 2, cx + uw / 2, cy + vh / 2, rgba(1, 1, 1, alpha));
        // second, slowly turning additive layer for motion
        float speed = th.fx == BgFx::Spiral ? 0.08f : (th.fx == BgFx::Bubbles ? 0.04f : 0.01f);
        float ang = time_ * speed;
        float diag = std::sqrt((float)w * w + (float)h * h) * 0.5f * 1.1f;
        Vec3 c((float)w / 2, (float)h / 2, 0);
        Vec3 rr(std::cos(ang) * diag, std::sin(ang) * diag, 0), uu(std::sin(ang) * diag, -std::cos(ang) * diag, 0);
        b.begin(tex, Blend::Additive, false);
        float a2 = (th.fx == BgFx::Spiral ? 0.35f : 0.16f) * alpha;
        b.quad(c, rr, uu, 0.5f - uw * 0.55f, 0.5f - uw * 0.55f * sa / ta, 0.5f + uw * 0.55f, 0.5f + uw * 0.55f * sa / ta, rgba(1, 1, 1, a2));
    };
    layer(theme_, 1.0f);
    if (themeFade_ < 1) layer(prevTheme_, 1.0f - themeFade_);

    // background particles
    float s = h / 1080.0f;
    b.begin(spriteTex_, Blend::Additive, false);
    for (const BgParticle& p : bgParts_) {
        float k = p.life / p.maxLife;
        float a = p.a * std::sin(kPi * k);
        if (theme(theme_).fx == BgFx::Fireflies) a *= 0.5f + 0.5f * std::sin(time_ * 3 + p.phase * 7);
        if (theme(theme_).fx == BgFx::Stars && p.spr == SprSparkle) a *= 0.6f + 0.4f * std::sin(time_ * 5 + p.phase * 9);
        float u0, v0, u1, v1;
        spriteUV(p.spr, u0, v0, u1, v1);
        float sz = p.size * s;
        float sx = sz;
        if (p.spr == SprCoin) sx *= std::fabs(std::cos(p.rot));
        Vec3 c(p.x * w, p.y * h, 0);
        float ca = std::cos(p.spr == SprCoin ? 0 : p.rot), sa = std::sin(p.spr == SprCoin ? 0 : p.rot);
        if (p.spr == SprStreak) sx = sz, sz = sz * 0.12f;
        Vec3 rr(ca * sx, sa * sx, 0), uu(sa * sz, -ca * sz, 0);
        bool alphaBlend = (p.spr == SprSpade || p.spr == SprHeart || p.spr == SprDiamond || p.spr == SprClub || p.spr == SprCoin);
        b.begin(spriteTex_, alphaBlend ? Blend::Alpha : Blend::Additive, false);
        b.quad(c, rr, uu, u0, v0, u1, v1, rgba(p.r, p.g, p.b, a));
    }
}

void App::drawBoard(Batch& b) {
    const Config& cfg = game_->config();
    const Theme& th = theme(theme_);
    float W = (float)cfg.width, H = (float)cfg.height;
    float u0, v0, u1, v1;

    // dark glass panel
    spriteUV(SprWhite, u0, v0, u1, v1);
    b.begin(spriteTex_, Blend::Alpha, true, true);
    b.rect(-0.12f, -0.12f, W + 0.12f, H + 0.12f, u0, v0, u1, v1, rgba(0.0f, 0.01f, 0.03f, 0.72f), -0.02f);
    // floor plate under the bottom row
    b.quad(Vec3(W * 0.5f, -0.02f, 0.55f), Vec3(W * 0.5f + 0.12f, 0, 0), Vec3(0, 0, -0.57f), u0, v0, u1, v1, rgba(0.02f, 0.03f, 0.06f, 0.85f));

    // grid lines
    b.begin(gridTex_, Blend::Additive, true, true, 1.0f);
    b.rect(0, 0, W, H, 0, 0, W, H, col(th.accent, 0.55f, 0.6f), -0.01f);

    // glowing frame
    b.begin(spriteTex_, Blend::Additive, true, true, 2.2f);
    Color8 fc = col(th.accent, 1);
    b.rect(-0.12f, -0.12f, -0.06f, H + 0.12f, u0, v0, u1, v1, fc, 0);
    b.rect(W + 0.06f, -0.12f, W + 0.12f, H + 0.12f, u0, v0, u1, v1, fc, 0);
    b.rect(-0.12f, H + 0.06f, W + 0.12f, H + 0.12f, u0, v0, u1, v1, fc, 0);
    // front edge of the floor
    b.quad(Vec3(W * 0.5f, -0.04f, 1.12f), Vec3(W * 0.5f + 0.12f, 0, 0), Vec3(0, 0.03f, 0), u0, v0, u1, v1, fc);
    // soft glow around the frame
    spriteUV(SprSoftSquare, u0, v0, u1, v1);
    b.begin(spriteTex_, Blend::Additive, true, true, 0.7f);
    b.rect(-0.6f, -0.6f, W + 0.6f, H + 0.6f, u0, v0, u1, v1, col(th.accent, 0.18f), -0.03f);

    // 3D box: side walls with depth and glowing front edges
    spriteUV(SprGradient, u0, v0, u1, v1);
    b.begin(spriteTex_, Blend::Additive, true, true, 0.8f);
    for (int side = 0; side < 2; ++side) {
        float x = side ? W + 0.06f : -0.06f;
        b.quad(Vec3(x, H * 0.5f, 0.55f), Vec3(0, 0, 0.57f), Vec3(0, H * 0.5f + 0.06f, 0), u0, v0, u1, v1, col(th.accent, 0.10f));
    }
    // depth lines on the side walls, one per row
    for (int r = 0; r <= cfg.height; ++r)
        for (int side = 0; side < 2; ++side) {
            float x = side ? W + 0.06f : -0.06f;
            line3(b, Vec3(x, (float)r, -0.02f), Vec3(x, (float)r, 1.1f), 0.012f, col(th.accent, r == 0 ? 0.6f : 0.18f));
        }
    for (int side = 0; side < 2; ++side) {
        float x = side ? W + 0.09f : -0.09f;
        line3(b, Vec3(x, -0.04f, 1.12f), Vec3(x, H + 0.09f, 1.12f), 0.03f, col(th.accent, 0.9f));
        line3(b, Vec3(x, H + 0.09f, -0.02f), Vec3(x, H + 0.09f, 1.12f), 0.03f, col(th.accent, 0.7f));
    }
    line3(b, Vec3(-0.09f, H + 0.09f, 1.12f), Vec3(W + 0.09f, H + 0.09f, 1.12f), 0.03f, col(th.accent, 0.7f));

    if (!game_->gameOver()) {
        // rising dice warning arrows (several columns at higher levels)
        float rf = clampf(game_->riseFrac(), 0, 1);
        float pulse = 0.5f + 0.5f * std::sin(time_ * (4 + rf * 14));
        Rgb ac = Rgb{lerpf(th.accent.r, 1.0f, rf), lerpf(th.accent.g, 0.2f, rf), lerpf(th.accent.b, 0.15f, rf)};
        bool showRise = !(helpWorld_ && !sceneRise_);
        for (int rc : game_->nextRiseCols()) {
            if (!showRise) break;
            spriteUV(SprArrow, u0, v0, u1, v1);
            b.begin(spriteTex_, Blend::Additive, true, false, 1.5f + rf * 2.0f * pulse);
            b.quad(Vec3(rc + 0.5f, -0.75f, 1.0f), Vec3(0.3f, 0, 0), Vec3(0, 0.36f, 0), u0, v0, u1, v1, col(ac, 0.6f + 0.4f * pulse));
            spriteUV(SprGradient, u0, v0, u1, v1);
            b.begin(spriteTex_, Blend::Additive, true, true, 1.0f + rf * 2);
            b.quad(Vec3(rc + 0.5f, 0.5f, 0.01f), Vec3(0.48f, 0, 0), Vec3(0, -0.5f, 0), u0, v0, u1, v1, col(ac, 0.15f + 0.35f * rf * rf));
        }
    }

    // glow behind combo dice
    spriteUV(SprSoftSquare, u0, v0, u1, v1);
    for (int r = 0; r < cfg.height; ++r)
        for (int c = 0; c < cfg.width; ++c) {
            const Cell& cl = game_->cell(c, r);
            if (!cl.occupied || cl.state == DieState::Normal) continue;
            bool red = cl.state == DieState::Red;
            float frac = game_->comboTimerFrac(cl.combo);
            float pulse = 0.5f + 0.5f * std::sin(time_ * (red ? 16 : 5 + 10 * (1 - frac)));
            Vec3 p = dieVisualPos(cl.uid, c, r);
            b.begin(spriteTex_, Blend::Additive, true, true, red ? 2.5f : 1.8f);
            Rgb gc = red ? Rgb{1.0f, 0.22f, 0.12f} : Rgb{0.25f, 0.6f, 1.0f};
            b.quad(Vec3(p.x, p.y, 0.01f), Vec3(0.75f, 0, 0), Vec3(0, 0.75f, 0), u0, v0, u1, v1, col(gc, 0.35f + 0.4f * pulse));
        }
}

void App::drawDice() {
    bool dead = game_->gameOver();
    const ActiveDie& a = game_->active();
    for (const auto& kv : vis_) {
        const VisDie& v = kv.second;
        float sc = 0.93f * easeOutBack(v.spawn);
        sc *= 1 + 0.06f * v.bounce * std::sin(v.bounce * 9);
        MeshMaterial m;
        m.tex = diceTex_;
        m.specular = 0.55f;
        m.shininess = 56;
        // find logical state for glow
        DieState st = DieState::Normal;
        int comboId = -1;
        int c = (int)std::floor(v.pos.x), r = (int)std::floor(v.pos.y + 0.5f);
        (void)r;
        bool isActive = a.present && kv.first == a.uid;
        if (!isActive) {
            for (int rr = 0; rr < game_->config().height && c >= 0 && c < game_->config().width; ++rr) {
                const Cell& cl = game_->cell(c, rr);
                if (cl.occupied && cl.uid == kv.first) { st = cl.state; comboId = cl.combo; break; }
            }
        }
        if (st == DieState::Blue) {
            float frac = game_->comboTimerFrac(comboId);
            float pulse = 0.5f + 0.5f * std::sin(time_ * (5 + 10 * (1 - frac)));
            m.emissive[0] = 0.02f; m.emissive[1] = 0.10f + 0.12f * pulse; m.emissive[2] = 0.30f + 0.30f * pulse;
            m.tint[0] = 0.75f; m.tint[1] = 0.85f; m.tint[2] = 1.0f;
            sc *= 1 + 0.035f * pulse;
        } else if (st == DieState::Red) {
            float pulse = 0.5f + 0.5f * std::sin(time_ * 16);
            m.emissive[0] = 0.35f + 0.35f * pulse; m.emissive[1] = 0.03f; m.emissive[2] = 0.02f;
            m.tint[0] = 1.0f; m.tint[1] = 0.7f; m.tint[2] = 0.65f;
            sc *= 1 + 0.05f * pulse;
        } else if (isActive) {
            m.emissive[0] = m.emissive[1] = m.emissive[2] = 0.06f;
        }
        if (dead) { m.tint[0] = m.tint[1] = m.tint[2] = 0.35f; }
        Mat4 model = Mat4::translate(v.pos) * v.rot.toMat4() * Mat4::scale(Vec3(sc, sc, sc));
        gfx_->drawMesh(diceMesh_, model.m, m);
    }
    for (const Fragment& f : frags_) {
        float k = f.life / f.maxLife;
        float sc = f.size * (1 - k * k);
        Rgb c = diceColor(f.value);
        MeshMaterial m;
        m.tex = diceTex_;
        m.emissive[0] = c.r * 2 * (1 - k); m.emissive[1] = c.g * 2 * (1 - k); m.emissive[2] = c.b * 2 * (1 - k);
        Mat4 model = Mat4::translate(f.p) * f.q.toMat4() * Mat4::scale(Vec3(sc, sc, sc));
        gfx_->drawMesh(diceMesh_, model.m, m);
    }
}

// Camera-facing 3D line (ribbon) from a to c.
void App::line3(Batch& b, const Vec3& a, const Vec3& c, float thick, Color8 colr) {
    float u0, v0, u1, v1;
    spriteUV(SprWhite, u0, v0, u1, v1);
    b.begin(spriteTex_, Blend::Additive, true, true, 1.6f);
    Vec3 mid = (a + c) * 0.5f, dir = (c - a) * 0.5f;
    Vec3 side = normalize(cross(dir, camPos_ - mid)) * thick;
    b.quad(mid, dir, side, u0, v0, u1, v1, colr);
}

// The lane from the falling die down to the cell it will land on: a glass tube
// in the column with light pulses running toward the target, and a wireframe
// "ghost" cube that flashes when a pulse arrives.
void App::drawLandingLane(Batch& b) {
    if (game_->gameOver() || screen_ == Screen::Title || screen_ == Screen::LevelSelect) return;
    const ActiveDie& a = game_->active();
    if (!a.present) return;
    auto it = vis_.find(a.uid);
    if (it == vis_.end()) return;
    const Theme& th = theme(theme_);
    float u0, v0, u1, v1;
    int land = game_->landingRow();
    float x0 = a.col + 0.04f, x1 = a.col + 0.96f, z0 = 0.03f, z1 = 1.04f;
    float top = it->second.pos.y - 0.5f, bot = (float)land;
    float len = top - bot;

    if (len > 0.05f) {
        // faint tube walls
        spriteUV(SprWhite, u0, v0, u1, v1);
        b.begin(spriteTex_, Blend::Additive, true, true, 1.0f);
        float my = (top + bot) * 0.5f, hy = len * 0.5f;
        b.quad(Vec3(a.col + 0.5f, my, z0), Vec3(0.46f, 0, 0), Vec3(0, hy, 0), u0, v0, u1, v1, col(th.accent, 0.06f));
        for (float x : {x0, x1})
            b.quad(Vec3(x, my, 0.5f * (z0 + z1)), Vec3(0, 0, 0.5f * (z1 - z0)), Vec3(0, hy, 0), u0, v0, u1, v1, col(th.accent, 0.05f));
        // rails on the four edges
        for (float x : {x0, x1})
            for (float z : {z0, z1})
                line3(b, Vec3(x, bot, z), Vec3(x, top, z), 0.012f, col(th.accent, z == z1 ? 0.45f : 0.25f));

        // pulses: rings travelling down the tube, faster when the die is fast
        float speed = clampf(2.0f / std::max(0.05f, game_->currentStepInterval()), 4.0f, 16.0f);
        const float spacing = 1.6f;
        float off = std::fmod(time_ * speed, spacing);
        for (float d = off; d < len; d += spacing) {
            float k = d / len;                        // 0 at the die, 1 at the target
            float y = top - d;
            float alpha = (0.25f + 0.75f * k) * std::min(1.0f, d * 3.0f);
            Color8 c = col(Rgb{lerpf(th.accent.r, 1, k * 0.5f), lerpf(th.accent.g, 1, k * 0.5f), lerpf(th.accent.b, 1, k * 0.5f)}, alpha);
            float t = 0.02f + 0.015f * k;
            line3(b, Vec3(x0, y, z1), Vec3(x1, y, z1), t, c);
            line3(b, Vec3(x0, y, z0), Vec3(x1, y, z0), t, c);
            line3(b, Vec3(x0, y, z0), Vec3(x0, y, z1), t, c);
            line3(b, Vec3(x1, y, z0), Vec3(x1, y, z1), t, c);
        }
    }

    // ghost cube at the landing cell; flashes each time a pulse arrives
    float speed = clampf(2.0f / std::max(0.05f, game_->currentStepInterval()), 4.0f, 16.0f);
    float arrive = len > 0.05f ? std::fmod(time_ * speed + 1.6f - std::fmod(len, 1.6f), 1.6f) / 1.6f : 0;
    float flash = std::exp(-arrive * 7.0f);
    float breathe = 0.5f + 0.5f * std::sin(time_ * 6);
    float e = 0.45f + 0.04f * breathe;
    Vec3 cc((float)a.col + 0.5f, (float)land + 0.5f, 0.5f);
    Color8 gc = col(Rgb{lerpf(th.accent.r, 1, flash), lerpf(th.accent.g, 1, flash), lerpf(th.accent.b, 1, flash)}, 0.45f + 0.5f * flash);
    float t = 0.018f + 0.02f * flash;
    Vec3 p[8];
    for (int i = 0; i < 8; ++i) p[i] = cc + Vec3((i & 1) ? e : -e, (i & 2) ? e : -e, (i & 4) ? e : -e);
    const int edges[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    for (auto& ed : edges) line3(b, p[ed[0]], p[ed[1]], t, gc);
    // landing glow on the floor of the target cell
    spriteUV(SprSoftSquare, u0, v0, u1, v1);
    b.begin(spriteTex_, Blend::Additive, true, true, 1.5f + flash * 2);
    b.quad(Vec3(cc.x, (float)land + 0.02f, 0.5f), Vec3(0.6f, 0, 0), Vec3(0, 0, 0.6f), u0, v0, u1, v1, col(th.accent, 0.3f + 0.4f * flash));
    b.quad(Vec3(cc.x, cc.y, 0.0f), Vec3(0.6f, 0, 0), Vec3(0, 0.6f, 0), u0, v0, u1, v1, col(th.accent, 0.15f + 0.3f * flash));
}

void App::drawWorldFx(Batch& b) {
    const Config& cfg = game_->config();
    const Theme& th = theme(theme_);
    float W = (float)cfg.width;
    float u0, v0, u1, v1;

    // dice preview line (Standard mode only, like the original)
    const ActiveDie& a = game_->active();
    if (a.present && mode_ == Mode::Standard) {
        auto pv = game_->preview();
        b.begin(diceTex_, Blend::Alpha, true, true);
        for (size_t i = 0; i < pv.size(); ++i) {
            diceTileUV(pv[i].value, u0, v0, u1, v1);
            float k = 1.0f - 0.5f * (float)i / std::max<size_t>(1, pv.size());
            float sz = 0.24f;
            b.quad(Vec3(a.col + 0.5f, pv[i].row + 0.5f, 0.5f), Vec3(sz, 0, 0), Vec3(0, sz, 0), u0, v0, u1, v1,
                   rgba(1, 1, 1, 0.55f * k));
        }
        if (!pv.empty()) {
            spriteUV(SprHex, u0, v0, u1, v1);
            b.begin(spriteTex_, Blend::Additive, true, true, 2.0f);
            b.quad(Vec3(a.col + 0.5f, pv.back().row + 0.5f, 0.52f), Vec3(0.42f, 0, 0), Vec3(0, 0.42f, 0), u0, v0, u1, v1, col(th.accent, 0.8f));
        }
    }
    // glow under the active die
    if (a.present) {
        auto it = vis_.find(a.uid);
        if (it != vis_.end()) {
            spriteUV(SprDot, u0, v0, u1, v1);
            b.begin(spriteTex_, Blend::Additive, true, true, 1.2f);
            Vec3 p = it->second.pos;
            b.quad(Vec3(p.x, p.y, 0.02f), Vec3(1.1f, 0, 0), Vec3(0, 1.1f, 0), u0, v0, u1, v1, rgba(1, 1, 1, 0.35f));
        }
    }

    drawLandingLane(b);

    // particles (camera-facing)
    for (int pass = 0; pass < 2; ++pass) {
        for (const Particle& p : parts_) {
            if (p.additive != (pass == 1)) continue;
            float k = p.life / p.maxLife;
            float sz = lerpf(p.size0, p.size1, k);
            float alpha = p.a * (1 - k);
            spriteUV(p.spr, u0, v0, u1, v1);
            b.begin(spriteTex_, p.additive ? Blend::Additive : Blend::Alpha, true, true, p.inten);
            float cr = std::cos(p.rot), sr = std::sin(p.rot);
            Vec3 rr = (camRight_ * cr + camUp_ * sr) * sz, uu = (camUp_ * cr - camRight_ * sr) * sz;
            b.quad(p.p, rr, uu, u0, v0, u1, v1, rgba(p.r, p.g, p.b, alpha));
        }
    }

    // wipe line (always on top)
    if (!game_->gameOver()) {
        float y = game_->wipeY();
        spriteUV(SprGradient, u0, v0, u1, v1);
        b.begin(spriteTex_, Blend::Additive, true, false, 0.9f);
        b.quad(Vec3(W * 0.5f, y + 0.9f, 1.08f), Vec3(W * 0.5f + 0.1f, 0, 0), Vec3(0, 0.9f, 0), u0, v0, u1, v1, col(th.wipe, 0.35f));
        spriteUV(SprStreak, u0, v0, u1, v1);
        b.begin(spriteTex_, Blend::Additive, true, false, 3.0f);
        b.quad(Vec3(W * 0.5f, y, 1.1f), Vec3(W * 0.5f + 0.5f, 0, 0), Vec3(0, 0.35f, 0), u0, v0, u1, v1, col(th.wipe, 1));
        b.quad(Vec3(W * 0.5f, y, 1.1f), Vec3(W * 0.5f + 0.2f, 0, 0), Vec3(0, 0.08f, 0), u0, v0, u1, v1, rgba(1, 1, 1, 1));
    }
}

void App::drawPanel(Batch& b, float x0, float y0, float x1, float y1, float alpha) {
    const Theme& th = theme(theme_);
    b.begin(spriteTex_, Blend::Alpha, false);
    sprite(b, SprWhite, x0, y0, x1, y1, rgba(0.01f, 0.015f, 0.04f, 0.82f * alpha));
    float t = std::max(2.0f, (y1 - y0) * 0.004f);
    Color8 e = col(th.accent, 0.9f * alpha);
    sprite(b, SprWhite, x0, y0, x1, y0 + t, e);
    sprite(b, SprWhite, x0, y1 - t, x1, y1, e);
}

void App::drawHud(Batch& b, int w, int h) {
    float s = h / 1080.0f;
    const Theme& th = theme(theme_);
    Color8 label = rgba(0.75f, 0.8f, 0.9f, 0.9f);
    Color8 white = rgba(1, 1, 1, 1);
    float x = 48 * s;

    fontBody_.drawShadowed(b, "SCORE", x, 30 * s, 30 * s, label, Align::Left, 4 * s);
    fontBody_.drawShadowed(b, fmtNum((int64_t)displayScore_), x, 56 * s, 76 * s, white);

    fontBody_.drawShadowed(b, "MULTIPLIER", x, 165 * s, 30 * s, label, Align::Left, 4 * s);
    float mp = 1 + 0.25f * multPulse_;
    Rgb mc = game_->multiplier() > 1 ? th.accent : Rgb{1, 1, 1};
    fontBody_.drawShadowed(b, "x" + std::to_string(game_->multiplier()), x, 190 * s, 96 * s * mp, col(mc));
    int line = 0;
    auto sub = [&](const char* name, int v) {
        if (v <= 0) return; // like the original: hide counters you do not have
        fontBody_.drawShadowed(b, std::string(name) + "  x" + std::to_string(v), x, (310 + line * 42) * s, 34 * s, rgba(0.85f, 0.9f, 1, 0.95f), Align::Left, 2 * s);
        ++line;
    };
    sub("CHAIN", game_->chain());
    sub("WIPES", game_->wipes());
    sub("DROPS", game_->drops());

    // time
    bool quick = mode_ == Mode::QuickRun;
    float tval = quick ? game_->timeLeft() : game_->elapsed();
    fontBody_.drawShadowed(b, "TIME", x, h - 175 * s, 30 * s, label, Align::Left, 4 * s);
    Color8 tc = white;
    if (quick && tval < 30) tc = (std::fmod(time_, 0.5f) < 0.25f) ? rgba(1, 0.3f, 0.2f) : white;
    fontBody_.drawShadowed(b, fmtTime(tval), x, h - 145 * s, 84 * s, tc);
    if (quick && tval < 30 && tval > 0)
        fontTitle_.drawShadowed(b, "HURRY!", x + 260 * s, h - 128 * s, 40 * s, rgba(1, 0.35f, 0.2f, 0.6f + 0.4f * std::sin(time_ * 10)));

    // level
    char lv[8];
    std::snprintf(lv, sizeof lv, "%02d", game_->level());
    fontBody_.drawShadowed(b, "LEVEL", w - x, h - 175 * s, 30 * s, label, Align::Right, 4 * s);
    fontBody_.drawShadowed(b, lv, w - x, h - 145 * s, 84 * s, white, Align::Right);

    // theme + mode
    fontTitle_.drawShadowed(b, th.name, w - x, 34 * s, 44 * s, col(th.accent), Align::Right, 2 * s);
    fontBody_.drawShadowed(b, modeName(mode_), w - x, 88 * s, 28 * s, label, Align::Right, 6 * s);

    // danger vignette
    if (riseWarn_ > 0.01f) {
        b.begin(spriteTex_, Blend::Additive, false);
        float pulse = 0.5f + 0.5f * std::sin(time_ * 6);
        Color8 rc = rgba(1, 0.1f, 0.05f, riseWarn_ * (0.15f + 0.15f * pulse));
        sprite(b, SprGradient, 0, h * 0.7f, (float)w, (float)h, rc);
    }

    // popups
    for (const Popup& p : popups_) {
        float k = p.life / p.maxLife;
        float sx, sy;
        if (!project(p.world + Vec3(0, k * 1.2f, 0), sx, sy)) continue;
        float a = k < 0.7f ? 1 : 1 - (k - 0.7f) / 0.3f;
        float sz = 46 * s * p.size * (k < 0.15f ? easeOutBack(k / 0.15f) : 1);
        fontBody_.drawShadowed(b, p.text, sx, sy - sz * 0.5f, sz * 1.1f, col(p.color, a), Align::Center);
    }

    // banners
    for (const Banner& bn : banners_) {
        float k = bn.life / bn.maxLife;
        float a = k < 0.8f ? 1 : 1 - (k - 0.8f) / 0.2f;
        float sc = k < 0.12f ? easeOutBack(k / 0.12f) : 1;
        float sz = 110 * s * sc;
        float y = h * 0.4f;
        fontTitle_.drawShadowed(b, bn.text, w * 0.5f, y - sz * 0.5f, sz, col(bn.color, a), Align::Center, 6 * s);
        if (!bn.sub.empty())
            fontBody_.drawShadowed(b, bn.sub, w * 0.5f, y + sz * 0.6f, 40 * s, rgba(1, 1, 1, a), Align::Center, 8 * s);
    }
}

void App::drawTitleLogo(Batch& b, float cx, float y, float s) {
    const Theme& th = theme(theme_);
    std::string t = HX_GAME_NAME;
    float maxW = 640 * s;
    float sz = std::min(130 * s, maxW / std::max(1.0f, fontTitle_.width(t, 1.0f)));
    // soft glow
    for (int i = 0; i < 3; ++i) {
        float o = (i + 1) * 3 * s;
        fontTitle_.draw(b, t, cx + o * std::sin(time_ + i), y + 40 * s + o * std::cos(time_ * 1.3f + i), sz, col(th.accent, 0.12f), Align::Center, 8 * s);
    }
    fontTitle_.drawShadowed(b, t, cx, y + 40 * s, sz, rgba(1, 1, 1), Align::Center, 8 * s);
    // a row of six pips under the name (hexa = six)
    float pulse = 0.85f + 0.15f * std::sin(time_ * 3);
    b.begin(spriteTex_, Blend::Alpha, false);
    for (int i = 0; i < 6; ++i) {
        float px = cx + (i - 2.5f) * 44 * s, py = y + 40 * s + sz * 1.3f;
        float r = (11 + 3 * std::sin(time_ * 3 - i * 0.6f)) * s;
        sprite(b, SprDot, px - r, py - r, px + r, py + r, rgba(1, 0.2f * pulse, 0.18f));
    }
    fontBody_.drawShadowed(b, "inspired by Hexaxis XXI by Tim Hackett / Darksoft", cx, y + 40 * s + sz * 1.3f + 36 * s, 24 * s,
                           rgba(1, 1, 1, 0.55f), Align::Center, 2 * s);
}

int* App::selVar() {
    switch (screen_) {
    case Screen::Title: return &menuSel_;
    case Screen::Options: return &optSel_;
    case Screen::Paused: return &pauseSel_;
    default: return nullptr;
    }
}

// A tap / click hits the topmost target drawn last frame: select it, then press its action.
void App::handleTap(bool inject[ActCount]) {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        const Hit& h = *it;
        if (in_.tapX < h.x0 || in_.tapX > h.x1 || in_.tapY < h.y0 || in_.tapY > h.y1) continue;
        if (h.sel >= 0) {
            if (int* v = selVar()) *v = h.sel;
        }
        inject[h.act] = true;
        return;
    }
}

// On-screen buttons for touch screens (pause while playing, back in menus).
void App::drawTouchButtons(Batch& b, int w, int h, float s) {
    (void)h;
    if (!in_.touchUi) return;
    const Theme& th = theme(theme_);
    float r = 46 * s;
    auto button = [&](float cx, float cy, const char* label, Action act) {
        b.begin(spriteTex_, Blend::Alpha, false);
        sprite(b, SprDot, cx - r, cy - r, cx + r, cy + r, rgba(0, 0, 0, 0.45f));
        b.begin(spriteTex_, Blend::Additive, false);
        sprite(b, SprRing, cx - r, cy - r, cx + r, cy + r, col(th.accent, 0.8f));
        fontTitle_.drawShadowed(b, label, cx, cy - 22 * s, 40 * s, rgba(1, 1, 1), Align::Center, 2 * s);
        addHit(cx - r * 1.4f, cy - r * 1.4f, cx + r * 1.4f, cy + r * 1.4f, -1, act);
    };
    // keep clear of the notch / system gesture strip at the screen edges
    float top = plat_->safeTop + 110 * s, left = plat_->safeLeft + 110 * s;
    if (screen_ == Screen::Playing) button(w * 0.5f, top, "II", ActPause);
    else if (screen_ != Screen::Title && screen_ != Screen::GameOver && screen_ != Screen::Paused) button(left, top, "<", ActBack);
}

void App::drawMenuList(Batch& b, const std::vector<std::string>& items, int sel, float cx, float y, float size, float s) {
    const Theme& th = theme(theme_);
    float step = size * 1.45f;
    for (size_t i = 0; i < items.size(); ++i) {
        bool on = (int)i == sel;
        float yy = y + i * step;
        if (on) {
            float wdt = fontTitle_.width(items[i], size) + 120 * s;
            b.begin(spriteTex_, Blend::Additive, false);
            sprite(b, SprSoftSquare, cx - wdt / 2, yy - size * 0.2f, cx + wdt / 2, yy + size * 1.25f, col(th.accent, 0.25f + 0.1f * std::sin(time_ * 5)));
            float ax = cx - wdt / 2 + 20 * s;
            fontTitle_.draw(b, ">", ax + 6 * s * std::sin(time_ * 6), yy, size, col(th.accent), Align::Left);
        }
        fontTitle_.drawShadowed(b, items[i], cx, yy, size, on ? rgba(1, 1, 1) : rgba(0.7f, 0.75f, 0.85f, 0.85f), Align::Center, 3 * s);
        float hw = fontTitle_.width(items[i], size) * 0.5f + 60 * s;
        addHit(cx - hw, yy - size * 0.2f, cx + hw, yy + size * 1.25f, (int)i, ActConfirm);
    }
}

void App::drawHelp(Batch& b, int w, int h, float s) {
    float pw = std::min((float)w * 0.5f, 900 * s), ph = 760 * s;
    float x0 = 40 * s, y0 = (h - ph) / 2;
    float cx = x0 + pw * 0.5f;
    drawPanel(b, x0, y0, x0 + pw, y0 + ph, 1);
    const HelpPage& p = kHelp[helpPage_];
    float ts = std::min(48 * s, (pw - 60 * s) / std::max(1.0f, fontTitle_.width(p.title, 1.0f)));
    fontTitle_.drawShadowed(b, p.title, cx, y0 + 50 * s, ts, rgba(1, 0.25f, 0.2f), Align::Center, 3 * s);
    std::string body = p.body;
    float y = y0 + 160 * s;
    size_t start = 0;
    while (start <= body.size()) {
        size_t nl = body.find('\n', start);
        std::string ln = body.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        fontBody_.drawShadowed(b, ln, cx, y, 34 * s, rgba(1, 1, 1), Align::Center);
        y += 48 * s;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    drawHelpInfo(b, cx, y0 + ph - 230 * s, s);
    // page dots
    float dw = 18 * s, dx = cx - (kHelpCount - 1) * dw * 0.5f;
    b.begin(spriteTex_, Blend::Alpha, false);
    for (int i = 0; i < kHelpCount; ++i) {
        float r = (i == helpPage_ ? 6.0f : 4.0f) * s;
        sprite(b, SprDot, dx + i * dw - r, y0 + ph - 120 * s - r, dx + i * dw + r, y0 + ph - 120 * s + r,
               i == helpPage_ ? col(theme(theme_).accent) : rgba(1, 1, 1, 0.35f));
    }
    char pg[48];
    std::snprintf(pg, sizeof pg, "<  %d OF %d  >", helpPage_ + 1, kHelpCount);
    fontBody_.drawShadowed(b, pg, cx, y0 + ph - 80 * s, 30 * s, rgba(0.75f, 0.8f, 0.9f), Align::Center, 4 * s);
    addHit(x0 + pw, 0, (float)w, (float)h, -1, ActRight);      // tap the live scene: next page
    addHit(cx - 220 * s, y0 + ph - 110 * s, cx - 40 * s, y0 + ph - 20 * s, -1, ActLeft);
    addHit(cx + 40 * s, y0 + ph - 110 * s, cx + 220 * s, y0 + ph - 20 * s, -1, ActRight);
}

void App::drawScores(Batch& b, int w, int h, float s) {
    addHit(0, 0, w / 3.0f, (float)h, -1, ActLeft);
    addHit(w / 3.0f, 0, w * 2 / 3.0f, (float)h, -1, ActConfirm);
    addHit(w * 2 / 3.0f, 0, (float)w, (float)h, -1, ActRight);
    float pw = 1100 * s, ph = 760 * s;
    float x0 = (w - pw) / 2, y0 = (h - ph) / 2;
    drawPanel(b, x0, y0, x0 + pw, y0 + ph, 1);
    const Theme& th = theme(theme_);
    fontTitle_.drawShadowed(b, "HI-SCORES", w * 0.5f, y0 + 36 * s, 60 * s, rgba(1, 1, 1), Align::Center, 6 * s);
    std::string tab = std::string("<   ") + modeName((Mode)scoreTab_) + "   >";
    fontTitle_.drawShadowed(b, tab, w * 0.5f, y0 + 120 * s, 36 * s, col(th.accent), Align::Center, 4 * s);
    float cx[5] = {x0 + 90 * s, x0 + 560 * s, x0 + 720 * s, x0 + 860 * s, x0 + 1020 * s};
    float y = y0 + 200 * s;
    Color8 lab = rgba(0.7f, 0.75f, 0.85f);
    fontBody_.draw(b, "#", cx[0], y, 28 * s, lab, Align::Left);
    fontBody_.draw(b, "SCORE", cx[1], y, 28 * s, lab, Align::Right);
    fontBody_.draw(b, "LEVEL", cx[2], y, 28 * s, lab, Align::Right);
    fontBody_.draw(b, "MULT", cx[3], y, 28 * s, lab, Align::Right);
    fontBody_.draw(b, "TIME", cx[4], y, 28 * s, lab, Align::Right);
    y += 50 * s;
    const auto& list = scores_[scoreTab_];
    if (list.empty()) fontBody_.drawShadowed(b, "NO RECORDS YET", w * 0.5f, y + 120 * s, 40 * s, rgba(1, 1, 1, 0.7f), Align::Center);
    for (size_t i = 0; i < list.size(); ++i) {
        bool hl = (int)i == scoreHighlight_;
        Color8 c = hl ? col(th.accent, 0.6f + 0.4f * std::sin(time_ * 6)) : rgba(1, 1, 1);
        float sz = 36 * s;
        fontBody_.drawShadowed(b, std::to_string(i + 1), cx[0], y, sz, c);
        fontBody_.drawShadowed(b, fmtNum(list[i].score), cx[1], y, sz, c, Align::Right);
        fontBody_.drawShadowed(b, std::to_string(list[i].level), cx[2], y, sz, c, Align::Right);
        fontBody_.drawShadowed(b, "x" + std::to_string(list[i].mult), cx[3], y, sz, c, Align::Right);
        fontBody_.drawShadowed(b, fmtTime((float)list[i].seconds), cx[4], y, sz, c, Align::Right);
        y += 46 * s;
    }
}

void App::drawLevelSelect(Batch& b, float cx, float y, float s) {
    const Theme& th = theme(theme_);
    int lv = settings_.startLevel;
    fontTitle_.drawShadowed(b, modeName(pendingMode_), cx, y, 44 * s, rgba(1, 1, 1), Align::Center, 4 * s);
    fontBody_.drawShadowed(b, "START LEVEL", cx, y + 80 * s, 34 * s, col(th.accent), Align::Center, 2 * s);
    char buf[64];
    std::snprintf(buf, sizeof buf, "<   %d   >", lv);
    float pulse = 0.5f + 0.5f * std::sin(time_ * 5);
    fontBody_.drawShadowed(b, buf, cx, y + 130 * s, 84 * s, rgba(1, 1, 1, 0.85f + 0.15f * pulse), Align::Center, 4 * s);
    addHit(cx - 260 * s, y + 110 * s, cx - 70 * s, y + 240 * s, -1, ActLeft);
    addHit(cx + 70 * s, y + 110 * s, cx + 260 * s, y + 240 * s, -1, ActRight);
    addHit(cx - 70 * s, y + 110 * s, cx + 70 * s, y + 240 * s, -1, ActConfirm);

    // speed bars: one per level, height = speed
    float bw = 22 * s, gap = 6 * s, total = kMaxStartLevel * (bw + gap) - gap;
    float bx = cx - total * 0.5f, by = y + 330 * s;
    b.begin(spriteTex_, Blend::Alpha, false);
    for (int i = 1; i <= kMaxStartLevel; ++i) {
        float hgt = (14 + 7.0f * i) * s;
        float x0 = bx + (i - 1) * (bw + gap);
        Rgb c = i <= lv ? Rgb{lerpf(th.accent.r, 1.0f, i / 15.0f), lerpf(th.accent.g, 0.3f, i / 15.0f), lerpf(th.accent.b, 0.2f, i / 15.0f)}
                        : Rgb{0.25f, 0.27f, 0.32f};
        sprite(b, SprWhite, x0, by - hgt, x0 + bw, by, col(c, i == lv ? 1.0f : 0.8f));
    }
    // numbers that matter
    Config cfg;
    cfg.startLevel = lv;
    Game g(cfg, pendingMode_, 1);
    std::snprintf(buf, sizeof buf, "Fall speed: %.2f s per step", g.fallInterval());
    fontBody_.drawShadowed(b, buf, cx, by + 30 * s, 28 * s, rgba(0.85f, 0.9f, 1), Align::Center, 2 * s);
    std::snprintf(buf, sizeof buf, "Random dice: %d every %.1f s", g.riseCount(), g.riseInterval());
    fontBody_.drawShadowed(b, buf, cx, by + 70 * s, 28 * s, rgba(0.85f, 0.9f, 1), Align::Center, 2 * s);
    const char* hint = lv <= 2  ? "Relaxed - learn how the dice roll"
                     : lv <= 5  ? "Normal"
                     : lv <= 8  ? "Fast"
                     : lv <= 11 ? "Expert"
                     : lv <= 13 ? "Speedster"
                                : "Impossible - for machines only";
    Rgb hc = lv <= 11 ? th.accent : (lv <= 13 ? Rgb{1.0f, 0.6f, 0.15f} : Rgb{1.0f, 0.2f, 0.15f});
    fontBody_.drawShadowed(b, hint, cx, by + 120 * s, 30 * s, col(hc), Align::Center, 2 * s);
    fontBody_.draw(b, in_.touchUi ? "tap  <  >  to change,  tap the number to start"
                                  : "LEFT / RIGHT  change      ENTER  start      ESC  back",
                   cx, by + 180 * s, 24 * s, rgba(1, 1, 1, 0.55f), Align::Center);
}

void App::drawMenus(Batch& b, int w, int h) {
    float s = h / 1080.0f;
    hits_.clear();
    const Theme& th = theme(theme_);
    switch (screen_) {
    case Screen::Title:
    case Screen::LevelSelect:
    case Screen::Options: {
        // left side darkening for readability
        b.begin(spriteTex_, Blend::Alpha, false);
        float pw = std::min((float)w * 0.55f, 1000 * s);
        sprite(b, SprWhite, 0, 0, pw * 0.75f, (float)h, rgba(0, 0, 0, 0.45f));
        float cx = pw * 0.42f;
        drawTitleLogo(b, cx, 90 * s, s);
        if (screen_ == Screen::Title) {
            std::vector<std::string> items = {"STANDARD", "ADVANCED", "QUICK RUN", "HI-SCORES", "HOW TO PLAY", "OPTIONS", "QUIT"};
            drawMenuList(b, items, menuSel_, cx, 470 * s, 48 * s, s);
            const char* subs[7] = {"Classic Mode with Dice Preview", "Pro Mode with no Dice Preview", "3 Minute Session",
                                   "View your Records", "Learn the Rules", "Sound, Display, Themes", "Leave the Game"};
            fontBody_.drawShadowed(b, subs[menuSel_], cx, 470 * s + 7 * 48 * s * 1.45f + 20 * s, 32 * s, col(th.accent), Align::Center, 2 * s);
        } else if (screen_ == Screen::LevelSelect) {
            drawLevelSelect(b, cx, 450 * s, s);
        } else {
            char buf[64];
            std::vector<std::string> items;
            std::snprintf(buf, sizeof buf, "MUSIC  < %d >", settings_.musicVol); items.push_back(buf);
            std::snprintf(buf, sizeof buf, "SOUND  < %d >", settings_.sfxVol); items.push_back(buf);
            items.push_back(std::string("FULLSCREEN  ") + (plat_->fullscreen || !plat_->hasWindow ? "ON" : "OFF"));
            items.push_back(std::string("THEME  < ") + (settings_.themeMode < 0 ? "BY LEVEL" : theme(settings_.themeMode).name) + " >");
            items.push_back("BACK");
            drawMenuList(b, items, optSel_, cx, 470 * s, 40 * s, s);
        }
        fontBody_.draw(b, HX_GAME_NAME " \xc2\xa9 " HX_REMAKE_YEAR " " HX_REMAKE_AUTHOR "   \xc2\xb7   inspired by Hexaxis XXI (PSP, 2007) by Tim Hackett / Darksoft", 30 * s, h - 50 * s, 24 * s, rgba(1, 1, 1, 0.5f));
        break;
    }
    case Screen::Paused: {
        b.begin(spriteTex_, Blend::Alpha, false);
        sprite(b, SprWhite, 0, 0, (float)w, (float)h, rgba(0, 0, 0, 0.45f));
        float pw = 760 * s, ph = 600 * s;
        float x0 = (w - pw) / 2, y0 = (h - ph) / 2;
        drawPanel(b, x0, y0, x0 + pw, y0 + ph, 1);
        fontTitle_.drawShadowed(b, "PAUSED", w * 0.5f, y0 + 40 * s, 72 * s, rgba(1, 1, 1), Align::Center, 10 * s);
        std::vector<std::string> items = {"RESUME", "RESTART", std::string("< ") + th.name + " >", "HOW TO PLAY", "MAIN MENU"};
        drawMenuList(b, items, pauseSel_, w * 0.5f, y0 + 190 * s, 40 * s, s);
        hits_[hits_.size() - 3].act = ActRight; // theme entry: tap = next theme
        break;
    }
    case Screen::Help:
        drawHelp(b, w, h, s);
        break;
    case Screen::Scores:
        drawScores(b, w, h, s);
        break;
    case Screen::GameOver: {
        b.begin(spriteTex_, Blend::Alpha, false);
        sprite(b, SprWhite, 0, 0, (float)w, (float)h, rgba(0, 0, 0, 0.5f));
        float pw = 900 * s, ph = 560 * s;
        float x0 = (w - pw) / 2, y0 = (h - ph) / 2;
        drawPanel(b, x0, y0, x0 + pw, y0 + ph, 1);
        bool timeUp = mode_ == Mode::QuickRun;
        fontTitle_.drawShadowed(b, timeUp ? "TIME UP!" : "GAME OVER", w * 0.5f, y0 + 40 * s, 80 * s,
                                timeUp ? rgba(1, 0.85f, 0.3f) : rgba(1, 0.25f, 0.2f), Align::Center, 8 * s);
        fontBody_.drawShadowed(b, fmtNum(game_->score()), w * 0.5f, y0 + 160 * s, 110 * s, rgba(1, 1, 1), Align::Center);
        char buf[96];
        std::snprintf(buf, sizeof buf, "LEVEL %d     BEST MULTIPLIER x%d     %s", game_->level(), game_->bestMultiplier(), fmtTime(game_->elapsed()).c_str());
        fontBody_.drawShadowed(b, buf, w * 0.5f, y0 + 300 * s, 34 * s, rgba(0.8f, 0.85f, 0.95f), Align::Center, 2 * s);
        if (lastRank_ >= 0) {
            std::snprintf(buf, sizeof buf, "NEW RECORD  #%d", lastRank_ + 1);
            fontTitle_.drawShadowed(b, buf, w * 0.5f, y0 + 370 * s, 48 * s, col(th.accent, 0.7f + 0.3f * std::sin(time_ * 6)), Align::Center, 4 * s);
        }
        addHit(0, 0, (float)w, (float)h, -1, ActConfirm);
        if (screenTime_ > 0.6f)
            fontBody_.drawShadowed(b, in_.touchUi ? "TAP TO CONTINUE" : "PRESS ENTER / (A)", w * 0.5f, y0 + ph - 80 * s, 30 * s, rgba(1, 1, 1, 0.6f + 0.4f * std::sin(time_ * 4)), Align::Center, 6 * s);
        break;
    }
    case Screen::Playing:
        break;
    }
}

// ====================================================================== audio

void App::pumpAudio() {
    Audio* au = plat_->audio;
    if (!au) return;
    int rate = au->sampleRate();
    int target = rate * 70 / 1000;
    int queued = au->queuedFrames();
    int need = target - queued;
    if (need <= 0) return;
    if (need > rate / 5) need = rate / 5;
    audioBuf_.resize((size_t)need * 2);
    synth_.render(audioBuf_.data(), need);
    au->push(audioBuf_.data(), need);
}

} // namespace hx
