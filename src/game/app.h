// The whole game presentation: menus, animation, effects, HUD, audio cues.
// Portable: talks to the hardware only through Platform (platform.h).
#pragma once
#include "../core/game.h"
#include "assets.h"
#include "autopilot.h"
#include "batch.h"
#include "font.h"
#include "hxmath.h"
#include "platform.h"
#include "synth.h"
#include "themes.h"
#include "worker.h"
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace hx {

struct Settings {
    int musicVol = 6, sfxVol = 8;  // 0..10
    bool fullscreen = false;
    int startLevel = 1;            // last chosen start level (1..15)
    int themeMode = -1;            // -1 = change with level, else fixed theme
};

struct ScoreEntry {
    int64_t score = 0;
    int level = 1, mult = 1, seconds = 0;
};

struct LaunchOptions {
    bool autoplay = false;         // computer plays a Standard game (testing)
    int theme = -1;
    bool skipTitle = false;
    Mode mode = Mode::Standard;
};

class App {
public:
    ~App();
    bool init(Platform* p, const LaunchOptions& opt);
    void frame(float dt, const InputState& in);
    bool quit() const { return quit_; }

private:
    enum class Screen { Title, LevelSelect, Playing, Paused, Help, Scores, Options, GameOver };

    struct VisDie {
        Vec3 pos;
        Quat rot;
        float vy = 0;
        float spawn = 1;           // 0..1 grow-in
        float bounce = 0;
        Vec3 fromPos, toPos;
        Quat fromRot, toRot;
        float animT = 1, animDur = 0.1f, animBump = 0;
        bool seen = false;
        int value = 1;
    };
    struct Particle {
        Vec3 p, v;
        float life = 0, maxLife = 1, size0 = 0.2f, size1 = 0, rot = 0, rotV = 0, grav = 0, drag = 0;
        float r = 1, g = 1, b = 1, a = 1, inten = 1;
        int spr = 0;
        bool additive = true;
    };
    struct Fragment {
        Vec3 p, v, axis;
        Quat q;
        float spin = 0, life = 0, maxLife = 1, size = 0.3f;
        int value = 1;
    };
    struct BgParticle {
        float x = 0, y = 0, vx = 0, vy = 0, size = 10, rot = 0, rotV = 0, phase = 0, life = 0, maxLife = 1;
        float r = 1, g = 1, b = 1, a = 1;
        int spr = 0;
    };
    struct Popup {
        Vec3 world;
        std::string text;
        float life = 0, maxLife = 1.2f, size = 1;
        Rgb color;
    };
    struct Banner {
        std::string text, sub;
        float life = 0, maxLife = 2;
        Rgb color;
    };

    // flow
    void startGame(Mode m, bool demo);
    struct RollMark { uint32_t uid; bool side; float dur; };
    // Everything that belongs to "the board on screen", so the help screen can
    // show its own animated example board and give the paused game back after.
    struct World {
        std::unique_ptr<Game> game;
        std::unordered_map<uint32_t, VisDie> vis;
        std::vector<Particle> parts;
        std::vector<Fragment> frags;
        std::vector<Popup> popups;
        std::vector<Banner> banners;
        std::vector<RollMark> rolled;
        float simAccum = 0, displayScore = 0, gameOverTimer = 0;
        int clearSeq = 0;
        bool demo = false, hardDropping = false;
        Mode mode = Mode::Standard;
        uint32_t spawnedUid = 0;
    };
    void swapWorld(World& w);
    void snapActiveVisual();
    // help scenes (help_scenes.cpp)
    struct PlanStep { int col; int front; float wait; float dropDelay; };
    struct PlanState { int next = 0, cur = -1; uint32_t uid = 0; float t = 0, moveT = 0, atColT = 0; bool dropped = false; };
    struct Cue { float t; int kind; int a, b, c; };
    void enterHelp(Screen ret);
    void leaveHelp();
    void buildHelpScene(int page);
    void updateHelpScene(float dt);
    void drawHelpInfo(Batch& b, float x, float y, float s);
    static Input drivePlan(const Game& g, const std::vector<PlanStep>& plan, PlanState& st, float dt);
    void updateTitle(float dt);
    void runDemo(float dt);
    void updateLevelSelect(float dt);
    void updatePlaying(float dt);
    void updatePaused();
    void updateHelp(float dt);
    void updateScores();
    void updateOptions();
    void updateGameOver(float dt);
    void stepGame(float dt, const Input& in);
    void handleEvent(const Event& e);
    void setTheme(int t);
    // Tap / click targets, collected while drawing the menus (used next frame).
    struct Hit { float x0, y0, x1, y1; int sel; Action act; };
    std::vector<Hit> hits_;
    void addHit(float x0, float y0, float x1, float y1, int sel, Action act) { hits_.push_back(Hit{x0, y0, x1, y1, sel, act}); }
    int* selVar();
    void handleTap(bool inject[ActCount]);
    void drawTouchButtons(Batch& b, int w, int h, float s);
    bool pressed(Action a) const { return pressed_[a]; }
    bool repeat(Action a) const { return repeat_[a]; }

    // visuals
    void updateVisuals(float dt);
    void updateBgParticles(float dt);
    Vec3 cellCenter(int c, int r) const { return Vec3(c + 0.5f, r + 0.5f, 0.5f); }
    Vec3 dieVisualPos(uint32_t uid, int c, int r) const;
    void burstDie(const Vec3& p, int value, bool big);
    void dust(const Vec3& p, int n);
    void ring(const Vec3& p, Rgb c, float size, float inten);
    void popup(const Vec3& p, const std::string& t, Rgb c, float size = 1);
    void banner(const std::string& t, const std::string& sub, Rgb c, float life = 2.0f);
    bool project(const Vec3& p, float& sx, float& sy) const;

    // rendering
    void render();
    void computeCamera(int w, int h);
    void drawBackground(Batch& b, int w, int h);
    void drawBoard(Batch& b);
    void drawDice();
    void drawWorldFx(Batch& b);
    void drawHud(Batch& b, int w, int h);
    void drawMenus(Batch& b, int w, int h);
    void drawPanel(Batch& b, float x0, float y0, float x1, float y1, float alpha);
    void drawMenuList(Batch& b, const std::vector<std::string>& items, int sel, float cx, float y, float size, float s);
    void drawLevelSelect(Batch& b, float cx, float y, float s);
    void line3(Batch& b, const Vec3& a, const Vec3& c, float thick, Color8 col);
    void drawLandingLane(Batch& b);
    void drawTitleLogo(Batch& b, float cx, float y, float s);
    void drawHelp(Batch& b, int w, int h, float s);
    void drawScores(Batch& b, int w, int h, float s);
    void sprite(Batch& b, int spr, float x0, float y0, float x1, float y1, Color8 c);

    // persistence
    void loadSettings();
    void saveSettings();
    void loadScores();
    void saveScores();
    int insertScore(Mode m, const ScoreEntry& e); // returns rank or -1
    void applyVolumes();

    void pumpAudio();
    void uploadBackgrounds();

    Platform* plat_ = nullptr;
    Renderer* gfx_ = nullptr;
    LaunchOptions opt_;
    bool quit_ = false;

    // resources
    MeshId diceMesh_ = 0;
    TexId diceTex_ = 0, spriteTex_ = 0, gridTex_ = 0;
    TexId bgTex_[kThemeCount] = {};
    Font fontTitle_, fontBody_;
    Synth synth_;
    std::vector<float> audioBuf_;

    // background generation (worker thread): 0 missing, 1 image ready, 2 uploaded
    std::vector<Image> bgImages_;
    std::atomic<int> bgReady_[kThemeCount];
    std::atomic<bool> bgStop_{false};
    float bgFadeIn_[kThemeCount] = {};
    Worker bgWorker_;

    // input
    bool prevHeld_[ActCount] = {};
    bool pressed_[ActCount] = {};
    bool repeat_[ActCount] = {};
    float repeatTimer_[ActCount] = {};
    InputState in_;

    // flow state
    Screen screen_ = Screen::Title;
    Screen helpReturn_ = Screen::Title;
    int menuSel_ = 0, pauseSel_ = 0, optSel_ = 0, helpPage_ = 0, scoreTab_ = 0, scoreHighlight_ = -1;
    float screenTime_ = 0, gameOverTimer_ = 0;
    bool demo_ = false;
    Autopilot pilot_;
    std::unique_ptr<Game> game_;
    Mode mode_ = Mode::Standard;
    float simAccum_ = 0;
    int theme_ = 0, pendingTheme_ = -1, prevTheme_ = 0;
    float themeFade_ = 1;
    Settings settings_;
    std::vector<ScoreEntry> scores_[3];
    int lastRank_ = -1;

    // visuals
    std::unordered_map<uint32_t, VisDie> vis_;
    std::vector<Particle> parts_;
    std::vector<Fragment> frags_;
    std::vector<BgParticle> bgParts_;
    std::vector<Popup> popups_;
    std::vector<Banner> banners_;
    float time_ = 0, shake_ = 0, zoom_ = 1.0f, displayScore_ = 0, titleShift_ = 1;
    float frameDt_ = 0.016f;
    World helpStash_;
    bool helpWorld_ = false, scenePilot_ = false, sceneRise_ = false;
    std::vector<PlanStep> plan_;
    PlanState planSt_;
    std::vector<Cue> cues_;
    size_t cueIdx_ = 0;
    float sceneT_ = 0, sceneLen_ = 1e9f, wipeHold_ = 0;
    int sceneInfo_ = 0; // 0 none, 1 score, 2 multiplier breakdown
    Mode pendingMode_ = Mode::Standard;
    float lookYaw_ = 0, lookPitch_ = 0, followYaw_ = 0;
    bool lookResetAnim_ = false;
    float bgSpawn_ = 0, flash_ = 0, multPulse_ = 0, riseWarn_ = 0;
    int clearSeq_ = 0;
    Vec3 lastClearPos_;
    std::vector<RollMark> rolled_; // dice that rolled since the last visual update
    bool hardDropping_ = false;
    uint32_t spawnedUid_ = 0;
    float blockedCooldown_ = 0;
    Rng fxRng_{777};

    // camera (computed each frame)
    Mat4 view_, proj_;
    Vec3 camPos_, camRight_, camUp_;
    int viewW_ = 1, viewH_ = 1;
};

} // namespace hx
