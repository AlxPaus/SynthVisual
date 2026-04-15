#pragma once
#include <vector>

struct CombFilter {
    std::vector<float> buf;
    int   wp      = 0;
    float fb      = 0.8f;
    float damping = 0.2f;
    float fs      = 0.0f;

    void init(int size) { buf.assign(size, 0.0f); }

    float process(float in) {
        float out = buf[wp];
        fs      = out * (1.0f - damping) + fs * damping;
        buf[wp] = in + fs * fb;
        wp = (wp + 1) % (int)buf.size();
        return out;
    }
};

struct AllPassFilter {
    std::vector<float> buf;
    int   wp = 0;
    float fb = 0.5f;

    void init(int size) { buf.assign(size, 0.0f); }

    float process(float in) {
        float bufOut = buf[wp];
        float out    = -in + bufOut;
        buf[wp] = in + bufOut * fb;
        wp = (wp + 1) % (int)buf.size();
        return out;
    }
};

struct ReverbFX {
    bool  enabled  = false;
    float roomSize = 0.8f;
    float damping  = 0.2f;
    float mix      = 0.3f;

    CombFilter    cL[4], cR[4];
    AllPassFilter aL[2], aR[2];

    ReverbFX() {
        // Schroeder topology: prime-spaced comb lengths, slight stereo spread via +23
        const int combSizes[4]    = { 1557, 1617, 1491, 1422 };
        const int allpassSizes[2] = { 225, 341 };
        for (int i = 0; i < 4; ++i) { cL[i].init(combSizes[i]); cR[i].init(combSizes[i] + 23); }
        for (int i = 0; i < 2; ++i) { aL[i].init(allpassSizes[i]); aR[i].init(allpassSizes[i] + 23); }
    }

    void process(float& inL, float& inR) {
        if (!enabled) return;
        float outL = 0.0f, outR = 0.0f;
        float imL = inL * 0.1f, imR = inR * 0.1f;

        for (int i = 0; i < 4; ++i) {
            cL[i].fb = roomSize; cL[i].damping = damping;
            cR[i].fb = roomSize; cR[i].damping = damping;
            outL += cL[i].process(imL);
            outR += cR[i].process(imR);
        }
        for (int i = 0; i < 2; ++i) {
            outL = aL[i].process(outL);
            outR = aR[i].process(outR);
        }

        inL = inL * (1.0f - mix) + outL * mix;
        inR = inR * (1.0f - mix) + outR * mix;
    }
};
