#pragma once
#include <algorithm>
#include <cmath>

struct GlobalEnv {
    int   state = 0;
    float val   = 0.0f;

    void trigger() { state = 1; val = 0.0f; }
    void release() { if (state != 0) state = 4; }

    void process(float a, float d, float s, float r, float sr) {
        a = std::max(0.0f, std::isfinite(a) ? a : 0.0f);
        d = std::max(0.0f, std::isfinite(d) ? d : 0.0f);
        s = std::clamp(std::isfinite(s) ? s : 0.0f, 0.0f, 1.0f);
        r = std::max(0.0f, std::isfinite(r) ? r : 0.0f);
        sr = std::max(1.0f, std::isfinite(sr) ? sr : 1.0f);
        if (state == 0) {
            val = 0.0f;
        } else if (state == 1) {
            val += 1.0f / (a * sr + 1.0f);
            if (val >= 1.0f) { val = 1.0f; state = 2; }
        } else if (state == 2) {
            val -= (1.0f - s) / (d * sr + 1.0f);
            if (val <= s) { val = s; state = 3; }
        } else if (state == 3) {
            val = s;
        } else if (state == 4) {
            val -= s / (r * sr + 1.0f);
            if (val <= 0.0f) { val = 0.0f; state = 0; }
        }
    }
};
