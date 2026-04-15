#pragma once
#include <cmath>
#include "constants.h"

enum LfoShape { LFO_SINE, LFO_TRIANGLE, LFO_SAW, LFO_SQUARE };

class AdvancedLFO {
public:
    float phase        = 0.0f;
    float rateHz       = 1.0f;
    float sampleRate   = 44100.0f;
    float currentValue = 0.0f;
    int   shape        = LFO_TRIANGLE;

    void advance(int frames) {
        phase += (rateHz / sampleRate) * frames;
        while (phase >= 1.0f) phase -= 1.0f;
        switch (shape) {
            case LFO_SINE:
                currentValue = std::sin(phase * 2.0f * PI);
                break;
            case LFO_TRIANGLE:
                if      (phase < 0.25f) currentValue = phase * 4.0f;
                else if (phase < 0.75f) currentValue = 1.0f - (phase - 0.25f) * 4.0f;
                else                   currentValue = -1.0f + (phase - 0.75f) * 4.0f;
                break;
            case LFO_SAW:
                currentValue = 1.0f - phase * 2.0f;
                break;
            case LFO_SQUARE:
                currentValue = (phase < 0.5f) ? 1.0f : -1.0f;
                break;
        }
    }
};
