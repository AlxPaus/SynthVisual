#pragma once
#include <cmath>
#include <algorithm>
#include "constants.h"

enum FilterType { FILT_LOWPASS, FILT_HIGHPASS, FILT_BANDPASS };

class BiquadFilter {
public:
    bool enabled;
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;

    // reset() clears delay-line history only, not the computed coefficients
    BiquadFilter() : enabled(false), b0(0), b1(0), b2(0), a1(0), a2(0) { reset(); }
    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

    void setCoefficients(FilterType type, float cutoffHz, float q, float sampleRate) {
        if (!enabled) return;
        float cutoff = std::min(cutoffHz, sampleRate * 0.49f);
        float w0    = 2.0f * PI * cutoff / sampleRate;
        float cosW0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * std::max(0.1f, q));
        float a0inv = 1.0f;

        switch (type) {
            case FILT_LOWPASS:
                b0 = (1.0f - cosW0) / 2.0f;
                b1 =  1.0f - cosW0;
                b2 = (1.0f - cosW0) / 2.0f;
                a0inv = 1.0f / (1.0f + alpha);
                a1 = -2.0f * cosW0;
                a2 =  1.0f - alpha;
                break;
            case FILT_HIGHPASS:
                b0 =  (1.0f + cosW0) / 2.0f;
                b1 = -(1.0f + cosW0);
                b2 =  (1.0f + cosW0) / 2.0f;
                a0inv = 1.0f / (1.0f + alpha);
                a1 = -2.0f * cosW0;
                a2 =  1.0f - alpha;
                break;
            case FILT_BANDPASS:
                b0 =  alpha;
                b1 =  0.0f;
                b2 = -alpha;
                a0inv = 1.0f / (1.0f + alpha);
                a1 = -2.0f * cosW0;
                a2 =  1.0f - alpha;
                break;
        }
        b0 *= a0inv; b1 *= a0inv; b2 *= a0inv;
        a1 *= a0inv; a2 *= a0inv;
    }

    // Frequency response magnitude at freq — used for the filter curve display
    float getMagnitude(float freq, float sampleRate) const {
        if (!enabled) return 1.0f;
        float w    = 2.0f * PI * freq / sampleRate;
        float cosw = std::cos(w),  cos2w = std::cos(2.0f * w);
        float sinw = std::sin(w),  sin2w = std::sin(2.0f * w);

        // Transfer function H(z) = numerator / denominator
        float numRe = b0 + b1 * cosw + b2 * cos2w;
        float numIm = -(b1 * sinw + b2 * sin2w);
        float denRe = 1.0f + a1 * cosw + a2 * cos2w;
        float denIm = -(a1 * sinw + a2 * sin2w);

        float denMagSq = denRe * denRe + denIm * denIm;
        if (denMagSq == 0.0f) return 0.0f;
        return std::sqrt((numRe * numRe + numIm * numIm) / denMagSq);
    }

    float process(float input) {
        if (!enabled) return input;
        float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = input;
        y2 = y1; y1 = output;
        if (std::abs(y1) < 1.0e-20f) y1 = 0.0f;  // flush denormals
        return output;
    }
};
