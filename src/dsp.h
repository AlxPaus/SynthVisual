#pragma once
#include <vector>
#include <cmath>
#include <cstdlib>
#include <complex>
#include <algorithm>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// 2 seconds of stereo delay buffer at 44100 Hz
constexpr int kMaxDelaySamples = 88200;

// === NOISE GENERATOR ===

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

// === GLOBAL ENVELOPE (for ENV2 / ENV3 in mod matrix) ===

struct GlobalEnv {
    int   state = 0;
    float val   = 0.0f;

    void trigger() { state = 1; val = 0.0f; }
    void release() { if (state != 0) state = 4; }

    void process(float a, float d, float s, float r, float sr) {
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

// === LFO ===

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

// === FX ===

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

// === FFT ===

// Iterative Cooley-Tukey FFT (in-place, complex input, power-of-2 size)
inline void computeFFT(std::vector<std::complex<float>>& data) {
    int n = (int)data.size();

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }

    // Butterfly passes
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * PI / len;
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + len / 2] * w;
                data[i + j]           = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
