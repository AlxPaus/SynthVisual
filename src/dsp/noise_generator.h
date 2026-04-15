#pragma once
#include <cstdlib>

struct NoiseGenerator {
    bool  enabled = false;
    float level   = 0.2f;
    int   type    = 0;  // 0=white, 1=pink, 2=brown

    float process() {
        if (!enabled) return 0.0f;
        float white = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        if (type == 0) {
            return white * level;
        } else if (type == 1) {
            // Voss/McCartney approximation of pink noise via 7-point filter
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            b6 = white * 0.115926f;
            return pink * 0.11f * level;
        } else {
            brownState = (brownState + 0.02f * white) / 1.02f;
            return brownState * 3.5f * level;
        }
    }

private:
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float brownState = 0;
};
