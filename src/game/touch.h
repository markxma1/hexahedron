// Portable touch gesture recognizer. A backend feeds raw finger events
// (any coordinate system in pixels), the game gets abstract actions.
//
//   one finger, drag left/right : one ActLeft/ActRight press per "step"
//   one finger, drag up         : one ActUp press per step
//   one finger, drag down       : ActDown held while the finger stays down
//   one finger, fast flick down : ActDrop press on release
//   one finger, short tap       : InputState::tap at the release point
//   two fingers, move           : look around (lookDX/lookDY)
//   two fingers, pinch          : zoom (zoomSteps)
//   two fingers, short tap      : lookReset
#pragma once
#include "platform.h"
#include <cstdint>

namespace hx {

class TouchGestures {
public:
    void setScreen(int w, int h);                     // pixels; step = 0.07 * h
    void down(int64_t id, float x, float y, double t); // t in seconds
    void move(int64_t id, float x, float y, double t);
    void up(int64_t id, float x, float y, double t);
    void cancelAll();
    // Once per frame, after feeding the events: adds this frame's actions to `in`
    // (only sets fields to true / adds to deltas, never clears what is there).
    void apply(InputState& in, double t);
    // True after any finger event in the last 15 s (show touch buttons).
    bool recentlyUsed(double t) const { return lastUse_ >= 0 && t - lastUse_ < 15.0; }

    struct Finger { bool on = false; int64_t id = 0; float x0 = 0, y0 = 0, x = 0, y = 0; double t0 = 0; };

private:
    static const int kMax = 4;
    Finger f_[kMax];
    int w_ = 1, h_ = 1;
    double lastUse_ = -1;

    // single-finger gesture
    int axis_ = 0;              // 0 undecided, 1 horizontal, 2 vertical
    float anchorX_ = 0, anchorY_ = 0;
    bool holdDown_ = false;
    bool multi_ = false;        // a second finger was involved: ignore until all fingers are up
    float histY_[8] = {};       // recent y samples for flick speed
    double histT_[8] = {};
    int histN_ = 0;

    // two-finger gesture
    bool twoActive_ = false;
    float midX_ = 0, midY_ = 0, dist_ = 0;
    float twoMoved_ = 0;
    double twoT0_ = 0;

    // output accumulated between apply() calls
    int pendingPulse_[ActCount] = {};
    bool pulsedLastFrame_[ActCount] = {};
    bool tap_ = false;
    float tapX_ = 0, tapY_ = 0;
    float lookDX_ = 0, lookDY_ = 0, zoom_ = 0;
    bool lookReset_ = false;
};

} // namespace hx
