#pragma once
#include <cmath>

struct DistortionFX {
    bool  enabled = false;
    float drive   = 0.0f;
    float mix     = 0.5f;

    float process(float in) {
        if (!enabled) return in;
        float df  = 1.0f + drive * 10.0f;
        float wet = std::atan(in * df) / std::atan(df);
        return in * (1.0f - mix) + wet * mix;
    }
};
