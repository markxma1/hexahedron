// Implementation of touch.h (first draft by the local coder model, reviewed + fixed).
#include "touch.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace hx {

void TouchGestures::setScreen(int w, int h) {
    w_ = w;
    h_ = h;
}

static int findFingerSlot(const TouchGestures::Finger* f, int max, int64_t id) {
    for (int i = 0; i < max; ++i) {
        if (f[i].on && f[i].id == id) return i;
    }
    return -1;
}

static int countActive(const TouchGestures::Finger* f, int max) {
    int c = 0;
    for (int i = 0; i < max; ++i) if (f[i].on) ++c;
    return c;
}

void TouchGestures::down(int64_t id, float x, float y, double t) {
    lastUse_ = t;
    
    // Find free slot or existing slot for this ID (should be free if valid state)
    int slot = -1;
    for (int i = 0; i < kMax; ++i) {
        if (!f_[i].on) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return; // No free slots
    
    
    f_[slot].on = true;
    f_[slot].id = id;
    f_[slot].x0 = x;
    f_[slot].y0 = y;
    f_[slot].x = x;
    f_[slot].y = y;
    f_[slot].t0 = t;
    
    int activeAfter = countActive(f_, kMax);
    
    if (activeAfter == 2) {
        // Second finger: start two-finger gesture
        multi_ = true;
        holdDown_ = false;
        axis_ = 0;
        
        // Find the two active fingers
        int i1 = -1, i2 = -1;
        for (int i = 0; i < kMax; ++i) {
            if (f_[i].on) {
                if (i1 == -1) i1 = i;
                else i2 = i;
            }
        }
        
        if (i1 != -1 && i2 != -1) {
            twoActive_ = true;
            midX_ = (f_[i1].x + f_[i2].x) / 2.0f;
            midY_ = (f_[i1].y + f_[i2].y) / 2.0f;
            float dx = f_[i2].x - f_[i1].x;
            float dy = f_[i2].y - f_[i1].y;
            dist_ = std::sqrt(dx * dx + dy * dy);
            twoMoved_ = 0;
            twoT0_ = t;
        }
    } else if (activeAfter == 1 && !multi_) {
        // First finger, no multi: start single-finger gesture
        axis_ = 0;
        anchorX_ = x;
        anchorY_ = y;
        histN_ = 0;
        // Push initial point into history
        int idx = histN_ % 8;
        histY_[idx] = y;
        histT_[idx] = t;
        histN_++;
    }
}

void TouchGestures::move(int64_t id, float x, float y, double t) {
    lastUse_ = t;
    
    int slot = findFingerSlot(f_, kMax, id);
    if (slot == -1) return;
    
    f_[slot].x = x;
    f_[slot].y = y;
    
    float h = static_cast<float>(h_);
    float u = 0.07f * h;
    
    int active = countActive(f_, kMax);
    
    if (twoActive_ && active >= 2) {
        // Two-finger gesture: update midpoint and distance
        int i1 = -1, i2 = -1;
        for (int i = 0; i < kMax; ++i) {
            if (f_[i].on) {
                if (i1 == -1) i1 = i;
                else i2 = i;
            }
        }
        
        if (i1 != -1 && i2 != -1) {
            float newMidX = (f_[i1].x + f_[i2].x) / 2.0f;
            float newMidY = (f_[i1].y + f_[i2].y) / 2.0f;
            float dx = f_[i2].x - f_[i1].x;
            float dy = f_[i2].y - f_[i1].y;
            float newDist = std::sqrt(dx * dx + dy * dy);
            
            // Look delta
            lookDX_ += (newMidX - midX_) / h;
            lookDY_ += (newMidY - midY_) / h;
            
            // Zoom if both distances > 1
            if (dist_ > 1.0f && newDist > 1.0f) {
                zoom_ += std::log(newDist / dist_) / 0.0726f;
            }
            
            twoMoved_ += std::abs(newMidX - midX_) + std::abs(newMidY - midY_) + std::abs(newDist - dist_);
            
            midX_ = newMidX;
            midY_ = newMidY;
            dist_ = newDist;
        }
    } else if (!multi_ && active == 1) {
        // Single-finger gesture
        
        // Push (y, t) into history ring
        int idx = histN_ % 8;
        histY_[idx] = y;
        histT_[idx] = t;
        histN_++;
        
        float dx = x - anchorX_;
        float dy = y - anchorY_;
        
        // Determine axis if undecided
        if (axis_ == 0) {
            float absDx = std::abs(dx);
            float absDy = std::abs(dy);
            if (std::max(absDx, absDy) > 0.3f * u) {
                axis_ = (absDx >= absDy) ? 1 : 2;
            }
        }
        
        // Process horizontal movement
        if (axis_ == 1) {
            while (dx >= u) {
                pendingPulse_[ActRight]++;
                anchorX_ += u;
                dx -= u;
            }
            while (dx <= -u) {
                pendingPulse_[ActLeft]++;
                anchorX_ -= u;
                dx += u;
            }
        }
        
        // Process vertical movement
        if (axis_ == 2) {
            while (dy <= -u) {
                pendingPulse_[ActUp]++;
                anchorY_ -= u;
                dy += u;
            }
            // Check for hold down: y - y0 >= u means moved down by at least one step from start
            if (y - f_[slot].y0 >= u) {
                holdDown_ = true;
            }
        }
    }
}

void TouchGestures::up(int64_t id, float x, float y, double t) {
    lastUse_ = t;
    
    int slot = findFingerSlot(f_, kMax, id);
    if (slot == -1) return;
    
    // Store final position for potential tap/flick calculation
    float fx = f_[slot].x0;
    float fy = f_[slot].y0;
    double ft0 = f_[slot].t0;
    
    // Push release point into history for flick detection
    if (!multi_) {
        int idx = histN_ % 8;
        histY_[idx] = y;
        histT_[idx] = t;
        histN_++;
    }
    
    f_[slot].on = false;
    
    int activeAfter = countActive(f_, kMax);
    
    if (twoActive_) {
        // Check if this was the last finger of the pair
        if (activeAfter == 0) {
            // Both fingers up: check for look reset (short tap with two fingers)
            if (t - twoT0_ < 0.3 && twoMoved_ < 0.4f * static_cast<float>(h_) * 0.07f) {
                lookReset_ = true;
            }
            twoActive_ = false;
            multi_ = false; // All fingers up, reset multi
        }
        // one finger left: keep the pair gesture open until it lifts too (two-finger tap)
    } else if (!multi_) {
        // Single-finger gesture end
        
        holdDown_ = false;
        
        float h = static_cast<float>(h_);
        float u = 0.07f * h;
        
        // Flick detection using history
        // Find samples within last 0.12s including release point
        // History is a ring buffer of up to 8 entries
        
        // Oldest and newest history samples inside the last 0.12 s (ring of 8, read in time order).
        int validN = std::min(histN_, 8);
        int start = histN_ > 8 ? histN_ % 8 : 0;
        int nValid = 0;
        float yOldest = 0, yLast = 0;
        double tOldest = 0, tLast = 0;
        for (int i = 0; i < validN; ++i) {
            int idx = (start + i) % 8;
            if (t - histT_[idx] > 0.12) continue;
            if (nValid == 0) { yOldest = histY_[idx]; tOldest = histT_[idx]; }
            yLast = histY_[idx]; tLast = histT_[idx];
            ++nValid;
        }
        if (nValid >= 2) {
            double dt = tLast - tOldest;
            if (dt <= 0) dt = (nValid - 1) * (1.0 / 60.0); // events without time steps (tests)
            float speed = (yLast - yOldest) / static_cast<float>(dt);
            if (speed > 1.5f * h && (y - fy) > 0.5f * u) pendingPulse_[ActDrop]++;
        }

        // Tap detection
        float distFromStart = std::sqrt((x - fx) * (x - fx) + (y - fy) * (y - fy));
        if (t - ft0 < 0.3 && distFromStart < 0.4f * u && axis_ == 0) {
            tap_ = true;
            tapX_ = x;
            tapY_ = y;
        }
        
        // Reset single-finger state
        axis_ = 0;
        histN_ = 0;
    }
}

void TouchGestures::cancelAll() {
    for (int i = 0; i < kMax; ++i) {
        f_[i].on = false;
    }
    multi_ = false;
    twoActive_ = false;
    holdDown_ = false;
    axis_ = 0;
    histN_ = 0;
    
    std::memset(pendingPulse_, 0, sizeof(pendingPulse_));
    std::memset(pulsedLastFrame_, 0, sizeof(pulsedLastFrame_));
    tap_ = false;
    lookDX_ = 0;
    lookDY_ = 0;
    zoom_ = 0;
    lookReset_ = false;
}

void TouchGestures::apply(InputState& in, double t) {
    // Process pending pulses
    for (int a = 0; a < ActCount; ++a) {
        if (pendingPulse_[a] > 0 && !pulsedLastFrame_[a]) {
            in.held[a] = true;
            pendingPulse_[a]--;
            pulsedLastFrame_[a] = true;
        } else {
            pulsedLastFrame_[a] = false;
        }
    }
    
    // Hold down
    if (holdDown_) {
        in.held[ActDown] = true;
    }
    
    // Tap
    if (tap_) {
        in.tap = true;
        in.tapX = tapX_;
        in.tapY = tapY_;
    }
    
    // Look deltas
    in.lookDX += lookDX_;
    in.lookDY += lookDY_;
    
    // Zoom steps
    in.zoomSteps += zoom_;
    
    // Look reset
    if (lookReset_) {
        in.lookReset = true;
    }
    
    // Touch UI visibility
    if (recentlyUsed(t)) {
        in.touchUi = true;
    }
    
    // Clear accumulated state
    tap_ = false;
    lookDX_ = 0;
    lookDY_ = 0;
    zoom_ = 0;
    lookReset_ = false;
}

} // namespace hx
