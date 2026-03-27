#pragma once
#include <vector>
#include <cmath>
#include <cstdlib>
#include <complex>
#include <algorithm>

struct NoiseGenerator {
    bool enabled = false; float level = 0.2f; int type = 0; 
    float b0=0, b1=0, b2=0, b3=0, b4=0, b5=0, b6=0; float brownState = 0;
    float process() {
        if (!enabled) return 0.0f; float white = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f);
        if (type == 0) return white * level; 
        else if (type == 1) { 
            b0 = 0.99886f * b0 + white * 0.0555179f; b1 = 0.99332f * b1 + white * 0.0750759f; b2 = 0.96900f * b2 + white * 0.1538520f; b3 = 0.86650f * b3 + white * 0.3104856f; b4 = 0.55000f * b4 + white * 0.5329522f; b5 = -0.7616f * b5 - white * 0.0168980f; float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f; b6 = white * 0.115926f; return pink * 0.11f * level; 
        } 
        else { brownState = (brownState + (0.02f * white)) / 1.02f; return brownState * 3.5f * level; }
    }
};

struct GlobalEnv {
    int state = 0; float val = 0.0f;
    void trigger() { state = 1; val = 0.0f; }
    void release() { if (state != 0) state = 4; }
    void process(float a, float d, float s, float r, float sr) {
        if (state == 0) { val = 0.0f; return; }
        if (state == 1) { val += 1.0f / (a * sr + 1.0f); if (val >= 1.0f) { val = 1.0f; state = 2; } }
        else if (state == 2) { val -= (1.0f - s) / (d * sr + 1.0f); if (val <= s) { val = s; state = 3; } }
        else if (state == 3) { val = s; }
        else if (state == 4) { val -= s / (r * sr + 1.0f); if (val <= 0.0f) { val = 0.0f; state = 0; } }
    }
};

enum LfoShape { LFO_SINE, LFO_TRIANGLE, LFO_SAW, LFO_SQUARE };
class AdvancedLFO {
public:
    float phase = 0.0f; float rateHz = 1.0f; float sampleRate = 44100.0f; float currentValue = 0.0f; int shape = LFO_TRIANGLE;
    void advance(int frames) {
        phase += (rateHz / sampleRate) * frames; while (phase >= 1.0f) phase -= 1.0f;
        if (shape == LFO_SINE) currentValue = std::sin(phase * 2.0f * 3.14159265f);
        else if (shape == LFO_TRIANGLE) { if (phase < 0.25f) currentValue = phase * 4.0f; else if (phase < 0.75f) currentValue = 1.0f - (phase - 0.25f) * 4.0f; else currentValue = -1.0f + (phase - 0.75f) * 4.0f; }
        else if (shape == LFO_SAW) currentValue = 1.0f - (phase * 2.0f); else if (shape == LFO_SQUARE) currentValue = (phase < 0.5f) ? 1.0f : -1.0f;
    }
};

struct DistortionFX { bool enabled=false; float drive=0.0f; float mix=0.5f; float process(float in){ if(!enabled) return in; float clean=in; float df=1.0f+drive*10.0f; float wet=std::atan(in*df)/std::atan(df); return clean*(1.0f-mix)+wet*mix; } };
struct DelayFX { bool enabled=false; float time=0.3f; float feedback=0.4f; float mix=0.3f; int writePos=0; std::vector<float> bufL, bufR; DelayFX(){bufL.resize(88200,0);bufR.resize(88200,0);} void process(float& inL, float& inR, float sr){ if(!enabled) return; int del=(int)(time*sr); if(del<=0) del=1; int rp=writePos-del; if(rp<0) rp+=(int)bufL.size(); float oL=bufL[rp], oR=bufR[rp]; bufL[writePos]=inL+oL*feedback; bufR[writePos]=inR+oR*feedback; writePos=(writePos+1)%bufL.size(); inL=inL*(1.0f-mix)+oL*mix; inR=inR*(1.0f-mix)+oR*mix; } };
struct CombFilter { std::vector<float> buf; int wp=0; float fb=0.8f, damping=0.2f, fs=0.0f; void init(int sz){buf.resize(sz,0);} float process(float in){ float out=buf[wp]; fs=(out*(1.0f-damping))+(fs*damping); buf[wp]=in+(fs*fb); wp=(wp+1)%buf.size(); return out; } };
struct AllPassFilter { std::vector<float> buf; int wp=0; float fb=0.5f; void init(int sz){buf.resize(sz,0);} float process(float in){ float bo=buf[wp]; float out=-in+bo; buf[wp]=in+(bo*fb); wp=(wp+1)%buf.size(); return out; } };
struct ReverbFX { bool enabled=false; float roomSize=0.8f, damping=0.2f, mix=0.3f; CombFilter cL[4], cR[4]; AllPassFilter aL[2], aR[2]; ReverbFX(){ int cs[4]={1557,1617,1491,1422}, as[2]={225,341}; for(int i=0;i<4;++i){cL[i].init(cs[i]);cR[i].init(cs[i]+23);} for(int i=0;i<2;++i){aL[i].init(as[i]);aR[i].init(as[i]+23);} } void process(float& inL, float& inR){ if(!enabled) return; float oL=0, oR=0, imL=inL*0.1f, imR=inR*0.1f; for(int i=0;i<4;++i){ cL[i].fb=roomSize; cL[i].damping=damping; cR[i].fb=roomSize; cR[i].damping=damping; oL+=cL[i].process(imL); oR+=cR[i].process(imR); } for(int i=0;i<2;++i){ oL=aL[i].process(oL); oR=aR[i].process(oR); } inL=inL*(1.0f-mix)+oL*mix; inR=inR*(1.0f-mix)+oR*mix; } };

inline void computeFFT(std::vector<std::complex<float>>& data) {
    int n = (int)data.size(); for (int i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(data[i], data[j]); }
    for (int len = 2; len <= n; len <<= 1) { float angle = -2.0f * 3.14159265f / len; std::complex<float> wlen(std::cos(angle), std::sin(angle)); for (int i = 0; i < n; i += len) { std::complex<float> w(1.0f, 0.0f); for (int j = 0; j < len / 2; j++) { std::complex<float> u = data[i + j]; std::complex<float> v = data[i + j + len / 2] * w; data[i + j] = u + v; data[i + j + len / 2] = u - v; w *= wlen; } } }
}