#pragma once
#include <vector>
#include <algorithm>
#include "constants.h"

struct DelayFX {
    bool  enabled  = false;
    float time     = 0.3f;
    float feedback = 0.4f;
    float mix      = 0.3f;
    int   writePos = 0;

    std::vector<float> bufL, bufR;

    DelayFX() {
        bufL.assign(kMaxDelaySamples, 0.0f);
        bufR.assign(kMaxDelaySamples, 0.0f);
    }

    void process(float& inL, float& inR, float sr) {
        if (!enabled) return;
        int delaySamples = std::max(1, (int)(time * sr));
        int readPos = writePos - delaySamples;
        if (readPos < 0) readPos += (int)bufL.size();

        float outL = bufL[readPos];
        float outR = bufR[readPos];

        bufL[writePos] = inL + outL * feedback;
        bufR[writePos] = inR + outR * feedback;
        writePos = (writePos + 1) % (int)bufL.size();

        inL = inL * (1.0f - mix) + outL * mix;
        inR = inR * (1.0f - mix) + outR * mix;
    }
};
