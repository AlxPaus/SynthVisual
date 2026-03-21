#define NOMINMAX 

#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h> 
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib") 

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <complex> 
#include <fstream>
#include <string>
#include <mutex>
#include <algorithm>

#include "wasapi_audio.h" 
#include "osc.h" 

const int MAX_VOICES = 128; 
std::mutex audioMutex; 

std::vector<WavetableOscillator> voicesA;
std::vector<WavetableOscillator> voicesB;
WavetableManager wtManager;

// === ГЛОБАЛЬНАЯ КЛАВИАТУРА И PORTAMENTO ===
int keyboardOctave = 4;
bool isMonoLegato = false;
float glideTime = 0.05f; 
std::vector<int> heldNotesStack;
float currentGlideFreq = 0.0f;
float targetGlideFreq = 0.0f;
float globalLastPolyFreq = 440.0f;

// === SUB & NOISE ===
bool subEnabled = false; int subOctave = -1; float subLevel = 0.5f; float subPhase = 0.0f;
struct NoiseGenerator {
    bool enabled = false; float level = 0.2f; int type = 0; 
    float b0=0, b1=0, b2=0, b3=0, b4=0, b5=0, b6=0; float brownState = 0;
    float process() {
        if (!enabled) return 0.0f; float white = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f);
        if (type == 0) return white * level; 
        else if (type == 1) { 
            b0 = 0.99886f * b0 + white * 0.0555179f; b1 = 0.99332f * b1 + white * 0.0750759f; 
            b2 = 0.96900f * b2 + white * 0.1538520f; b3 = 0.86650f * b3 + white * 0.3104856f; 
            b4 = 0.55000f * b4 + white * 0.5329522f; b5 = -0.7616f * b5 - white * 0.0168980f; 
            float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f; 
            b6 = white * 0.115926f; return pink * 0.11f * level; 
        } 
        else { brownState = (brownState + (0.02f * white)) / 1.02f; return brownState * 3.5f * level; }
    }
} globalNoise;

// === SERUM PARAMETERS ===
bool oscAEnabled = true; int currentTableIndexA = 0; int oscA_Octave = 0; int pitchSemiA = 0; float wtPosA = 0.0f; int unisonVoicesA = 1; float unisonDetuneA = 0.15f; float unisonBlendA = 0.75f; float oscALevel = 0.75f; 
bool oscBEnabled = false; int currentTableIndexB = 0; int oscB_Octave = 0; int pitchSemiB = 0; float wtPosB = 0.0f; int unisonVoicesB = 1; float unisonDetuneB = 0.15f; float unisonBlendB = 0.75f; float oscBLevel = 0.75f; 
float globalPhase = 0.0f; float globalPhaseRand = 100.0f; float masterVolumeDb = 0.0f; 
bool filterEnabled = false; int filterType = 0; float filterCutoff = 2000.0f; float filterResonance = 1.0f; 

// === MODULATION ENGINE ===
enum ModSource { SRC_NONE = 0, SRC_LFO1, SRC_LFO2, SRC_ENV1, SRC_ENV2, SRC_ENV3 };
enum ModTarget { TGT_NONE = 0, TGT_OSCA_WTPOS, TGT_OSCA_PITCH, TGT_OSCA_LEVEL, TGT_OSCA_DETUNE, TGT_OSCA_BLEND, TGT_OSCB_WTPOS, TGT_OSCB_PITCH, TGT_OSCB_LEVEL, TGT_OSCB_DETUNE, TGT_OSCB_BLEND, TGT_FILT_CUTOFF, TGT_FILT_RES, TGT_SUB_LEVEL, TGT_NOISE_LEVEL, TGT_DIST_DRIVE, TGT_DIST_MIX, TGT_DEL_TIME, TGT_DEL_FB, TGT_DEL_MIX, TGT_REV_SIZE, TGT_REV_DAMP, TGT_REV_MIX };

struct ModRouting { int source; int target; float amount; };
std::vector<ModRouting> modMatrix;
int uiSelectedModSource = SRC_LFO1; 

// === ENVELOPES & LFO ===
float envA[3] = {0.05f, 0.05f, 0.05f}; float envD[3] = {0.50f, 0.50f, 0.50f}; float envS[3] = {0.70f, 0.70f, 0.70f}; float envR[3] = {0.30f, 0.30f, 0.30f};

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
} auxEnvs[2];

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
} globalLFOs[2];

int lfoSyncMode[2] = {0, 0}; float globalBpm = 120.0f; float lfoRateHz[2] = {1.0f, 1.0f}; int lfoRateBPMIndex[2] = {2, 2};
bool noteState[512] = { false }; int keysPressedMidi[512]; 

// === EFFECTS ===
struct DistortionFX { bool enabled=false; float drive=0.0f; float mix=0.5f; float process(float in){ if(!enabled) return in; float clean=in; float df=1.0f+drive*10.0f; float wet=std::atan(in*df)/std::atan(df); return clean*(1.0f-mix)+wet*mix; } } globalDistortion;
struct DelayFX { bool enabled=false; float time=0.3f; float feedback=0.4f; float mix=0.3f; int writePos=0; std::vector<float> bufL, bufR; DelayFX(){bufL.resize(88200,0);bufR.resize(88200,0);} void process(float& inL, float& inR, float sr){ if(!enabled) return; int del=(int)(time*sr); if(del<=0) del=1; int rp=writePos-del; if(rp<0) rp+=(int)bufL.size(); float oL=bufL[rp], oR=bufR[rp]; bufL[writePos]=inL+oL*feedback; bufR[writePos]=inR+oR*feedback; writePos=(writePos+1)%bufL.size(); inL=inL*(1.0f-mix)+oL*mix; inR=inR*(1.0f-mix)+oR*mix; } } globalDelay;
struct CombFilter { std::vector<float> buf; int wp=0; float fb=0.8f, damping=0.2f, fs=0.0f; void init(int sz){buf.resize(sz,0);} float process(float in){ float out=buf[wp]; fs=(out*(1.0f-damping))+(fs*damping); buf[wp]=in+(fs*fb); wp=(wp+1)%buf.size(); return out; } };
struct AllPassFilter { std::vector<float> buf; int wp=0; float fb=0.5f; void init(int sz){buf.resize(sz,0);} float process(float in){ float bo=buf[wp]; float out=-in+bo; buf[wp]=in+(bo*fb); wp=(wp+1)%buf.size(); return out; } };
struct ReverbFX { bool enabled=false; float roomSize=0.8f, damping=0.2f, mix=0.3f; CombFilter cL[4], cR[4]; AllPassFilter aL[2], aR[2]; ReverbFX(){ int cs[4]={1557,1617,1491,1422}, as[2]={225,341}; for(int i=0;i<4;++i){cL[i].init(cs[i]);cR[i].init(cs[i]+23);} for(int i=0;i<2;++i){aL[i].init(as[i]);aR[i].init(as[i]+23);} } void process(float& inL, float& inR){ if(!enabled) return; float oL=0, oR=0, imL=inL*0.1f, imR=inR*0.1f; for(int i=0;i<4;++i){ cL[i].fb=roomSize; cL[i].damping=damping; cR[i].fb=roomSize; cR[i].damping=damping; oL+=cL[i].process(imL); oR+=cR[i].process(imR); } for(int i=0;i<2;++i){ oL=aL[i].process(oL); oR=aR[i].process(oR); } inL=inL*(1.0f-mix)+oL*mix; inR=inR*(1.0f-mix)+oR*mix; } } globalReverb;

const int SCOPE_SIZE = 512; std::vector<float> scopeBuffer(SCOPE_SIZE, 0.0f); int scopeIndex = 0; bool scopeTriggered = false; float lastScopeSample = 0.0f;
const int FFT_SIZE = 2048; std::vector<float> fftRingBuffer(FFT_SIZE, 0.0f); int fftRingPos = 0; std::vector<float> spectrumSmooth(FFT_SIZE / 2, 0.0f);

void computeFFT(std::vector<std::complex<float>>& data) {
    int n = (int)data.size(); for (int i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(data[i], data[j]); }
    for (int len = 2; len <= n; len <<= 1) { float angle = -2.0f * 3.14159265f / len; std::complex<float> wlen(std::cos(angle), std::sin(angle)); for (int i = 0; i < n; i += len) { std::complex<float> w(1.0f, 0.0f); for (int j = 0; j < len / 2; j++) { std::complex<float> u = data[i + j]; std::complex<float> v = data[i + j + len / 2] * w; data[i + j] = u + v; data[i + j + len / 2] = u - v; w *= wlen; } } }
}

void UpdateOscillatorsTable() { for(auto& v : voicesA) v.setWavetable(&wtManager.tables[currentTableIndexA]); for(auto& v : voicesB) v.setWavetable(&wtManager.tables[currentTableIndexB]); }
void UpdateEnvelopes() { for(auto& v : voicesA) v.env.setParameters(envA[0], envD[0], envS[0], envR[0]); for(auto& v : voicesB) v.env.setParameters(envA[0], envD[0], envS[0], envR[0]); }
void UpdateFilters() { for(auto& v : voicesA) v.filter.enabled = filterEnabled; for(auto& v : voicesB) v.filter.enabled = filterEnabled; }

void TriggerPool(std::vector<WavetableOscillator>& pool, int baseNote, int osc_Octave, int semiOffset, int unisonCount, float detune, float blend, bool legatoBypassTrigger = false) {
    int voicesToTrigger = unisonCount;
    for (auto& v : pool) {
        if (!v.isActive() || legatoBypassTrigger) {
            if (!legatoBypassTrigger) v.noteOn(baseNote); 
            float spreadStr = (unisonCount > 1) ? 2.0f * (float)(unisonCount - voicesToTrigger) / (float)(unisonCount - 1) - 1.0f : 0.0f;
            float detuneSemitones = spreadStr * (detune * 0.5f);
            float freq = 440.0f * std::pow(2.0f, (baseNote + (osc_Octave * 12) + semiOffset - 69.0f + detuneSemitones) / 12.0f);
            v.setFrequency(freq); v.setPan(spreadStr * blend);
            if (!legatoBypassTrigger) { float basePhaseIndex = (globalPhase / 360.0f) * TABLE_SIZE; float randOffset = ((float)rand() / (float)RAND_MAX) * (globalPhaseRand / 100.0f) * TABLE_SIZE; v.setPhase(std::fmod(basePhaseIndex + randOffset, (float)TABLE_SIZE)); }
            voicesToTrigger--; if (voicesToTrigger <= 0) break; 
        }
    }
}

void NoteOn(int note) {
    std::lock_guard<std::mutex> lock(audioMutex); 
    if (note < 0 || note > 127 || noteState[note]) return;
    noteState[note] = true; 
    if (isMonoLegato) {
        heldNotesStack.push_back(note); targetGlideFreq = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); globalLastPolyFreq = targetGlideFreq;
        if (heldNotesStack.size() == 1) { 
            currentGlideFreq = targetGlideFreq; auxEnvs[0].trigger(); auxEnvs[1].trigger();
            if (oscAEnabled) TriggerPool(voicesA, note, oscA_Octave, pitchSemiA, unisonVoicesA, unisonDetuneA, unisonBlendA); if (oscBEnabled) TriggerPool(voicesB, note, oscB_Octave, pitchSemiB, unisonVoicesB, unisonDetuneB, unisonBlendB);
        } else { 
            if (oscAEnabled) TriggerPool(voicesA, note, oscA_Octave, pitchSemiA, unisonVoicesA, unisonDetuneA, unisonBlendA, true); if (oscBEnabled) TriggerPool(voicesB, note, oscB_Octave, pitchSemiB, unisonVoicesB, unisonDetuneB, unisonBlendB, true);
        }
    } else {
        globalLastPolyFreq = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); auxEnvs[0].trigger(); auxEnvs[1].trigger();
        if (oscAEnabled) TriggerPool(voicesA, note, oscA_Octave, pitchSemiA, unisonVoicesA, unisonDetuneA, unisonBlendA); if (oscBEnabled) TriggerPool(voicesB, note, oscB_Octave, pitchSemiB, unisonVoicesB, unisonDetuneB, unisonBlendB);
    }
}

void NoteOff(int note) {
    std::lock_guard<std::mutex> lock(audioMutex); if (note < 0 || note > 127) return; noteState[note] = false; 
    if (isMonoLegato) {
        auto it = std::find(heldNotesStack.begin(), heldNotesStack.end(), note); if (it != heldNotesStack.end()) heldNotesStack.erase(it);
        if (heldNotesStack.empty()) { for (auto& v : voicesA) v.noteOff(); for (auto& v : voicesB) v.noteOff(); auxEnvs[0].release(); auxEnvs[1].release(); } 
        else { int prevNote = heldNotesStack.back(); targetGlideFreq = 440.0f * std::pow(2.0f, (prevNote - 69.0f) / 12.0f); globalLastPolyFreq = targetGlideFreq; }
    } else {
        for (auto& v : voicesA) if (v.getNote() == note) v.noteOff(); for (auto& v : voicesB) if (v.getNote() == note) v.noteOff();
        bool anyActive = false; for(auto& v : voicesA) if(v.isActive()) anyActive=true; if(!anyActive){auxEnvs[0].release(); auxEnvs[1].release();}
    }
}

HMIDIIN hMidiIn = nullptr; std::vector<std::string> midiPorts; int selectedMidiPort = -1;
void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) { if (wMsg == MIM_DATA) { unsigned char status = dwParam1 & 0xFF; unsigned char note = (dwParam1 >> 8) & 0xFF; unsigned char velocity = (dwParam1 >> 16) & 0xFF; unsigned char type = status & 0xF0; if (type == 0x90 && velocity > 0) NoteOn(note); else if (type == 0x80 || (type == 0x90 && velocity == 0)) NoteOff(note); } }
void RefreshMidiPorts() { midiPorts.clear(); UINT numDevs = midiInGetNumDevs(); for (UINT i = 0; i < numDevs; i++) { MIDIINCAPSA caps; if (midiInGetDevCapsA(i, &caps, sizeof(MIDIINCAPSA)) == MMSYSERR_NOERROR) { midiPorts.push_back(caps.szPname); } } }
void OpenMidiPort(int portIndex) { if (hMidiIn) { midiInStop(hMidiIn); midiInClose(hMidiIn); hMidiIn = nullptr; } if (portIndex >= 0 && portIndex < (int)midiPorts.size()) { if (midiInOpen(&hMidiIn, portIndex, (DWORD_PTR)MidiInProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) { midiInStart(hMidiIn); } } }

float GetModSum(int target_id) {
    float sum = 0.0f;
    for (auto& m : modMatrix) {
        if (m.target == target_id) {
            float srcVal = 0.0f;
            if (m.source == SRC_LFO1) srcVal = globalLFOs[0].currentValue;
            if (m.source == SRC_LFO2) srcVal = globalLFOs[1].currentValue;
            if (m.source == SRC_ENV1) { float maxEnv = 0.0f; for (int v = 0; v < MAX_VOICES; ++v) if (voicesA[v].isActive()) maxEnv = std::max(maxEnv, voicesA[v].env.currentLevel); srcVal = maxEnv; }
            if (m.source == SRC_ENV2) srcVal = auxEnvs[0].val;
            if (m.source == SRC_ENV3) srcVal = auxEnvs[1].val;
            sum += srcVal * m.amount;
        }
    }
    return sum;
}
float GetModAmountForUI(int target_id) { for (auto& m : modMatrix) if (m.source == uiSelectedModSource && m.target == target_id) return m.amount; return 0.0f; }

void data_callback(float* pOut, int frameCount) {
    std::lock_guard<std::mutex> lock(audioMutex); 
    for (int i = 0; i < frameCount * 2; ++i) pOut[i] = 0.0f;
    
    for (int i=0; i<2; ++i) {
        if (lfoSyncMode[i] == 1) { float beatHz = globalBpm / 60.0f; float mults[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f }; globalLFOs[i].rateHz = beatHz * mults[lfoRateBPMIndex[i]]; } 
        else { globalLFOs[i].rateHz = lfoRateHz[i]; }
        globalLFOs[i].advance(frameCount);
    }

    float mod_FiltCut = GetModSum(TGT_FILT_CUTOFF); float mod_FiltRes = GetModSum(TGT_FILT_RES);
    float mod_WTA = GetModSum(TGT_OSCA_WTPOS); float mod_LevA = GetModSum(TGT_OSCA_LEVEL); float mod_DetA = GetModSum(TGT_OSCA_DETUNE); float mod_BndA = GetModSum(TGT_OSCA_BLEND); float mod_PitA = GetModSum(TGT_OSCA_PITCH);
    float mod_WTB = GetModSum(TGT_OSCB_WTPOS); float mod_LevB = GetModSum(TGT_OSCB_LEVEL); float mod_DetB = GetModSum(TGT_OSCB_DETUNE); float mod_BndB = GetModSum(TGT_OSCB_BLEND); float mod_PitB = GetModSum(TGT_OSCB_PITCH);

    float currentModCutoff = std::max(20.0f, std::min(20000.0f, filterCutoff * std::pow(2.0f, mod_FiltCut * 5.0f)));
    float currentModRes = std::max(0.1f, std::min(10.0f, filterResonance + mod_FiltRes * 5.0f));
    float currentWTPosA = std::max(0.0f, std::min(1.0f, wtPosA + mod_WTA)); float currentLevelA = std::max(0.0f, std::min(1.0f, oscALevel + mod_LevA)); float currentDetA = std::max(0.0f, std::min(1.0f, unisonDetuneA + mod_DetA)); float currentBndA = std::max(0.0f, std::min(1.0f, unisonBlendA + mod_BndA));
    float currentWTPosB = std::max(0.0f, std::min(1.0f, wtPosB + mod_WTB)); float currentLevelB = std::max(0.0f, std::min(1.0f, oscBLevel + mod_LevB)); float currentDetB = std::max(0.0f, std::min(1.0f, unisonDetuneB + mod_DetB)); float currentBndB = std::max(0.0f, std::min(1.0f, unisonBlendB + mod_BndB));

    voicesA[0].setWTPos(currentWTPosA); voicesA[0].filter.setCoefficients((FilterType)filterType, currentModCutoff, currentModRes, 44100.0f);
    voicesB[0].setWTPos(currentWTPosB); voicesB[0].filter.setCoefficients((FilterType)filterType, currentModCutoff, currentModRes, 44100.0f);
    for(size_t j = 1; j < voicesA.size(); ++j) { if (voicesA[j].isActive()) { voicesA[j].setWTPos(currentWTPosA); voicesA[j].filter.setCoefficients((FilterType)filterType, currentModCutoff, currentModRes, 44100.0f); } }
    for(size_t j = 1; j < voicesB.size(); ++j) { if (voicesB[j].isActive()) { voicesB[j].setWTPos(currentWTPosB); voicesB[j].filter.setCoefficients((FilterType)filterType, currentModCutoff, currentModRes, 44100.0f); } }

    float gcA = 1.0f / std::sqrt((float)unisonVoicesA); float gcB = 1.0f / std::sqrt((float)unisonVoicesB); float mGain = std::pow(10.0f, masterVolumeDb / 20.0f); 
    float glideAlpha = 1.0f; if (glideTime > 0.001f) glideAlpha = 1.0f - std::exp(-1.0f / (44100.0f * (glideTime / 3.0f)));

    for (int i = 0; i < frameCount; ++i) {
        auxEnvs[0].process(envA[1], envD[1], envS[1], envR[1], 44100.0f);
        auxEnvs[1].process(envA[2], envD[2], envS[2], envR[2], 44100.0f);

        if (isMonoLegato && !heldNotesStack.empty()) {
            currentGlideFreq += glideAlpha * (targetGlideFreq - currentGlideFreq); globalLastPolyFreq = currentGlideFreq; 
            if (oscAEnabled) { for (int u = 0; u < unisonVoicesA; ++u) { if (voicesA[u].isActive()) { float spreadStr = (unisonVoicesA > 1) ? 2.0f * (float)(unisonVoicesA - 1 - u) / (float)(unisonVoicesA - 1) - 1.0f : 0.0f; float pitchMult = std::pow(2.0f, (oscA_Octave * 12 + pitchSemiA + mod_PitA*24.0f + (spreadStr * currentDetA * 0.5f)) / 12.0f); voicesA[u].setFrequency(currentGlideFreq * pitchMult); voicesA[u].setPan(spreadStr * currentBndA); } } }
            if (oscBEnabled) { for (int u = 0; u < unisonVoicesB; ++u) { if (voicesB[u].isActive()) { float spreadStr = (unisonVoicesB > 1) ? 2.0f * (float)(unisonVoicesB - 1 - u) / (float)(unisonVoicesB - 1) - 1.0f : 0.0f; float pitchMult = std::pow(2.0f, (oscB_Octave * 12 + pitchSemiB + mod_PitB*24.0f + (spreadStr * currentDetB * 0.5f)) / 12.0f); voicesB[u].setFrequency(currentGlideFreq * pitchMult); voicesB[u].setPan(spreadStr * currentBndB); } } }
        } else {
            if (oscAEnabled) { for (size_t u = 0; u < voicesA.size(); ++u) { if (voicesA[u].isActive()) { float freq = 440.0f * std::pow(2.0f, (voicesA[u].getNote() + oscA_Octave * 12 + pitchSemiA + mod_PitA*24.0f - 69.0f) / 12.0f); voicesA[u].setFrequency(freq); } } }
            if (oscBEnabled) { for (size_t u = 0; u < voicesB.size(); ++u) { if (voicesB[u].isActive()) { float freq = 440.0f * std::pow(2.0f, (voicesB[u].getNote() + oscB_Octave * 12 + pitchSemiB + mod_PitB*24.0f - 69.0f) / 12.0f); voicesB[u].setFrequency(freq); } } }
        }

        float mixL = 0.0f, mixR = 0.0f; float maxEnv = 0.0f; 
        if (oscAEnabled) { for (auto& v : voicesA) { if (v.isActive()) { float p = v.getPan(); float s = v.getSample() * currentLevelA; mixL += s * (1.0f-p)*0.5f*gcA*0.15f; mixR += s * (1.0f+p)*0.5f*gcA*0.15f; maxEnv = std::max(maxEnv, v.env.currentLevel); } } }
        if (oscBEnabled) { for (auto& v : voicesB) { if (v.isActive()) { float p = v.getPan(); float s = v.getSample() * currentLevelB; mixL += s * (1.0f-p)*0.5f*gcB*0.15f; mixR += s * (1.0f+p)*0.5f*gcB*0.15f; maxEnv = std::max(maxEnv, v.env.currentLevel); } } }

        float mod_SubLev = GetModSum(TGT_SUB_LEVEL); float mod_NoiseLev = GetModSum(TGT_NOISE_LEVEL);
        if (subEnabled && maxEnv > 0.001f) { float subF = globalLastPolyFreq * std::pow(2.0f, (float)subOctave); subPhase += subF / 44100.0f; if (subPhase >= 1.0f) subPhase -= 1.0f; float subS = std::sin(subPhase * 2.0f * 3.14159265f) * std::max(0.0f, std::min(1.0f, subLevel + mod_SubLev)) * maxEnv * 0.2f; mixL += subS; mixR += subS; }
        if (globalNoise.enabled && maxEnv > 0.001f) { float nS = globalNoise.process() * std::max(0.0f, std::min(1.0f, globalNoise.level + mod_NoiseLev)) * maxEnv * 0.15f; mixL += nS; mixR += nS; }

        globalDistortion.drive = std::max(0.0f, std::min(1.0f, globalDistortion.drive + GetModSum(TGT_DIST_DRIVE))); globalDistortion.mix = std::max(0.0f, std::min(1.0f, globalDistortion.mix + GetModSum(TGT_DIST_MIX)));
        globalDelay.time = std::max(0.01f, std::min(1.5f, globalDelay.time + GetModSum(TGT_DEL_TIME))); globalDelay.feedback = std::max(0.0f, std::min(0.95f, globalDelay.feedback + GetModSum(TGT_DEL_FB))); globalDelay.mix = std::max(0.0f, std::min(1.0f, globalDelay.mix + GetModSum(TGT_DEL_MIX)));
        globalReverb.roomSize = std::max(0.0f, std::min(0.98f, globalReverb.roomSize + GetModSum(TGT_REV_SIZE))); globalReverb.damping = std::max(0.0f, std::min(1.0f, globalReverb.damping + GetModSum(TGT_REV_DAMP))); globalReverb.mix = std::max(0.0f, std::min(1.0f, globalReverb.mix + GetModSum(TGT_REV_MIX)));

        mixL = globalDistortion.process(mixL); mixR = globalDistortion.process(mixR);
        globalDelay.process(mixL, mixR, 44100.0f); globalReverb.process(mixL, mixR); 
        
        float finalL = mixL * mGain; float finalR = mixR * mGain;
        pOut[2*i] = finalL; pOut[2*i+1] = finalR;

        float monoSample = (finalL + finalR) * 0.5f;
        if (!scopeTriggered && lastScopeSample <= 0.0f && monoSample > 0.0f) { scopeTriggered = true; scopeIndex = 0; }
        if (scopeTriggered && scopeIndex < SCOPE_SIZE) scopeBuffer[scopeIndex++] = monoSample;
        if (scopeIndex >= SCOPE_SIZE) scopeTriggered = false; 
        lastScopeSample = monoSample; fftRingBuffer[fftRingPos] = monoSample; fftRingPos = (fftRingPos + 1) % FFT_SIZE;
    }
}

// === ПРЕСЕТЫ ===
std::string SaveFileDialog() { char filename[MAX_PATH] = ""; OPENFILENAMEA ofn; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = NULL; ofn.lpstrFilter = "Osci Preset (*.osci)\0*.osci\0All Files (*.*)\0*.*\0"; ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH; ofn.lpstrTitle = "Save Preset"; ofn.lpstrDefExt = "osci"; ofn.Flags = OFN_DONTADDTORECENT | OFN_OVERWRITEPROMPT; if (GetSaveFileNameA(&ofn)) return std::string(filename); return ""; }
std::string OpenFileDialog() { char filename[MAX_PATH] = ""; OPENFILENAMEA ofn; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = NULL; ofn.lpstrFilter = "Osci Preset (*.osci)\0*.osci\0All Files (*.*)\0*.*\0"; ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH; ofn.lpstrTitle = "Load Preset"; ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST; if (GetOpenFileNameA(&ofn)) return std::string(filename); return ""; }

void SavePreset(const std::string& filename) {
    std::ofstream out(filename); if (!out) return;
    out << "modMatrixSize=" << modMatrix.size() << "\n";
    for(size_t i=0; i<modMatrix.size(); ++i) { out << "mod_" << i << "=" << modMatrix[i].source << "," << modMatrix[i].target << "," << modMatrix[i].amount << "\n"; }
    out << "masterVolumeDb=" << masterVolumeDb << "\n"; out << "isMonoLegato=" << isMonoLegato << "\n"; out << "glideTime=" << glideTime << "\n";
    out << "subEnabled=" << subEnabled << "\n"; out << "subOctave=" << subOctave << "\n"; out << "subLevel=" << subLevel << "\n";
    out << "noiseEnabled=" << globalNoise.enabled << "\n"; out << "noiseLevel=" << globalNoise.level << "\n"; out << "noiseType=" << globalNoise.type << "\n";
    out << "oscAEnabled=" << oscAEnabled << "\n"; out << "currentTableIndexA=" << currentTableIndexA << "\n"; out << "oscA_Octave=" << oscA_Octave << "\n"; out << "pitchSemiA=" << pitchSemiA << "\n"; out << "wtPosA=" << wtPosA << "\n"; out << "unisonVoicesA=" << unisonVoicesA << "\n"; out << "unisonDetuneA=" << unisonDetuneA << "\n"; out << "unisonBlendA=" << unisonBlendA << "\n"; out << "oscALevel=" << oscALevel << "\n";
    out << "oscBEnabled=" << oscBEnabled << "\n"; out << "currentTableIndexB=" << currentTableIndexB << "\n"; out << "oscB_Octave=" << oscB_Octave << "\n"; out << "pitchSemiB=" << pitchSemiB << "\n"; out << "wtPosB=" << wtPosB << "\n"; out << "unisonVoicesB=" << unisonVoicesB << "\n"; out << "unisonDetuneB=" << unisonDetuneB << "\n"; out << "unisonBlendB=" << unisonBlendB << "\n"; out << "oscBLevel=" << oscBLevel << "\n";
    for(int i=0; i<3; ++i) { out << "envA_" << i << "=" << envA[i] << "\n"; out << "envD_" << i << "=" << envD[i] << "\n"; out << "envS_" << i << "=" << envS[i] << "\n"; out << "envR_" << i << "=" << envR[i] << "\n"; }
    out << "filterEnabled=" << filterEnabled << "\n"; out << "filterType=" << filterType << "\n"; out << "filterCutoff=" << filterCutoff << "\n"; out << "filterResonance=" << filterResonance << "\n";
    for(int i=0; i<2; ++i) { out << "lfoShape_" << i << "=" << globalLFOs[i].shape << "\n"; out << "lfoSyncMode_" << i << "=" << lfoSyncMode[i] << "\n"; out << "lfoRateHz_" << i << "=" << lfoRateHz[i] << "\n"; out << "lfoRateBPMIndex_" << i << "=" << lfoRateBPMIndex[i] << "\n"; }
    out << "globalBpm=" << globalBpm << "\n"; out << "distEnabled=" << globalDistortion.enabled << "\n"; out << "distDrive=" << globalDistortion.drive << "\n"; out << "distMix=" << globalDistortion.mix << "\n";
    out << "delEnabled=" << globalDelay.enabled << "\n"; out << "delTime=" << globalDelay.time << "\n"; out << "delFB=" << globalDelay.feedback << "\n"; out << "delMix=" << globalDelay.mix << "\n";
    out << "revEnabled=" << globalReverb.enabled << "\n"; out << "revSize=" << globalReverb.roomSize << "\n"; out << "revDamp=" << globalReverb.damping << "\n"; out << "revMix=" << globalReverb.mix << "\n";
}

void LoadPreset(const std::string& filename) {
    std::ifstream in(filename); if (!in) return; std::string line; std::lock_guard<std::mutex> lock(audioMutex); modMatrix.clear();
    try {
        while (std::getline(in, line)) {
            size_t pos = line.find('='); if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos); std::string val = line.substr(pos + 1); if (val.empty()) continue;
            if (key.rfind("mod_", 0) == 0 && key != "modMatrixSize") {
                size_t c1 = val.find(','); size_t c2 = val.rfind(',');
                if (c1 != std::string::npos && c2 != std::string::npos) modMatrix.push_back({std::stoi(val.substr(0, c1)), std::stoi(val.substr(c1+1, c2-c1-1)), std::stof(val.substr(c2+1))});
            }
            else if (key == "masterVolumeDb") masterVolumeDb = std::stof(val); else if (key == "isMonoLegato") isMonoLegato = std::stoi(val); else if (key == "glideTime") glideTime = std::stof(val);
            else if (key == "subEnabled") subEnabled = std::stoi(val); else if (key == "subOctave") subOctave = std::stoi(val); else if (key == "subLevel") subLevel = std::stof(val);
            else if (key == "noiseEnabled") globalNoise.enabled = std::stoi(val); else if (key == "noiseLevel") globalNoise.level = std::stof(val); else if (key == "noiseType") globalNoise.type = std::stoi(val);
            else if (key == "oscAEnabled") oscAEnabled = std::stoi(val); else if (key == "currentTableIndexA") currentTableIndexA = std::stoi(val); else if (key == "oscA_Octave") oscA_Octave = std::stoi(val); else if (key == "pitchSemiA") pitchSemiA = std::stoi(val); else if (key == "wtPosA") wtPosA = std::stof(val); else if (key == "unisonVoicesA") unisonVoicesA = std::stoi(val); else if (key == "unisonDetuneA") unisonDetuneA = std::stof(val); else if (key == "unisonBlendA") unisonBlendA = std::stof(val); else if (key == "oscALevel") oscALevel = std::stof(val);
            else if (key == "oscBEnabled") oscBEnabled = std::stoi(val); else if (key == "currentTableIndexB") currentTableIndexB = std::stoi(val); else if (key == "oscB_Octave") oscB_Octave = std::stoi(val); else if (key == "pitchSemiB") pitchSemiB = std::stoi(val); else if (key == "wtPosB") wtPosB = std::stof(val); else if (key == "unisonVoicesB") unisonVoicesB = std::stoi(val); else if (key == "unisonDetuneB") unisonDetuneB = std::stof(val); else if (key == "unisonBlendB") unisonBlendB = std::stof(val); else if (key == "oscBLevel") oscBLevel = std::stof(val);
            else if (key.rfind("envA_", 0) == 0) envA[std::stoi(key.substr(5))] = std::stof(val); else if (key.rfind("envD_", 0) == 0) envD[std::stoi(key.substr(5))] = std::stof(val); else if (key.rfind("envS_", 0) == 0) envS[std::stoi(key.substr(5))] = std::stof(val); else if (key.rfind("envR_", 0) == 0) envR[std::stoi(key.substr(5))] = std::stof(val);
            else if (key == "filterEnabled") filterEnabled = std::stoi(val); else if (key == "filterType") filterType = std::stoi(val); else if (key == "filterCutoff") filterCutoff = std::stof(val); else if (key == "filterResonance") filterResonance = std::stof(val);
            else if (key.rfind("lfoShape_", 0) == 0) globalLFOs[std::stoi(key.substr(9))].shape = std::stoi(val); else if (key.rfind("lfoSyncMode_", 0) == 0) lfoSyncMode[std::stoi(key.substr(12))] = std::stoi(val); else if (key.rfind("lfoRateHz_", 0) == 0) lfoRateHz[std::stoi(key.substr(10))] = std::stof(val); else if (key.rfind("lfoRateBPMIndex_", 0) == 0) lfoRateBPMIndex[std::stoi(key.substr(16))] = std::stoi(val); else if (key == "globalBpm") globalBpm = std::stof(val);
            else if (key == "distEnabled") globalDistortion.enabled = std::stoi(val); else if (key == "distDrive") globalDistortion.drive = std::stof(val); else if (key == "distMix") globalDistortion.mix = std::stof(val);
            else if (key == "delEnabled") globalDelay.enabled = std::stoi(val); else if (key == "delTime") globalDelay.time = std::stof(val); else if (key == "delFB") globalDelay.feedback = std::stof(val); else if (key == "delMix") globalDelay.mix = std::stof(val);
            else if (key == "revEnabled") globalReverb.enabled = std::stoi(val); else if (key == "revSize") globalReverb.roomSize = std::stof(val); else if (key == "revDamp") globalReverb.damping = std::stof(val); else if (key == "revMix") globalReverb.mix = std::stof(val);
        }
    } catch (...) { std::cerr << "Preset corrupted!" << std::endl; }
    UpdateOscillatorsTable(); UpdateEnvelopes(); UpdateFilters();
}

// === КРУТИЛКА С DRAG & DROP ===
bool DrawKnob(const char* label, float* p_value, float v_min, float v_max, const char* format, int target_id, bool logScale = false) {
    ImGuiIO& io = ImGui::GetIO(); float radius = 20.0f; ImVec2 pos = ImGui::GetCursorScreenPos(); ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);
    ImGui::InvisibleButton(label, ImVec2(radius*2, radius*2 + 15)); bool value_changed = false;
    if (ImGui::IsItemActive() && io.MouseDelta.y != 0.0f) { float step = (v_max - v_min) * 0.005f; *p_value -= io.MouseDelta.y * step; *p_value = std::max(v_min, std::min(v_max, *p_value)); value_changed = true; }
    
    if (target_id != TGT_NONE && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MOD_SRC")) {
            int src = *(const int*)payload->Data;
            bool found = false; for (auto& m : modMatrix) { if (m.source == src && m.target == target_id) { m.amount = 0.5f; found = true; break; } }
            if (!found) modMatrix.push_back({src, target_id, 0.5f});
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList(); float angle_min = 3.141592f * 0.75f; float angle_max = 3.141592f * 2.25f;
    float t = logScale ? (std::log(*p_value/v_min) / std::log(v_max/v_min)) : ((*p_value - v_min) / (v_max - v_min)); float angle = angle_min + (angle_max - angle_min) * t;
    draw_list->AddCircleFilled(center, radius, IM_COL32(40,40,45,255), 32); draw_list->AddCircle(center, radius, IM_COL32(15,15,15,255), 32, 2.0f);
    draw_list->PathArcTo(center, radius - 2.0f, angle_min, angle, 32); draw_list->PathStroke(IM_COL32(0,200,255,255), false, 4.0f);
    
    float mod_val = GetModAmountForUI(target_id);
    if (mod_val != 0.0f) {
        float mod_t = std::max(0.0f, std::min(1.0f, t + mod_val)); float angle_mod = angle_min + (angle_max - angle_min) * mod_t;
        if (mod_val > 0) draw_list->PathArcTo(center, radius + 4.0f, angle, angle_mod, 16); else draw_list->PathArcTo(center, radius + 4.0f, angle_mod, angle, 16); draw_list->PathStroke(IM_COL32(255,150,0,255), false, 2.0f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) { char buf[32]; snprintf(buf, sizeof(buf), format, *p_value); ImVec2 tSize = ImGui::CalcTextSize(buf); draw_list->AddText(ImVec2(center.x - tSize.x/2, pos.y + radius*2 + 2), IM_COL32(255,255,255,255), buf);
    } else { ImVec2 tSize = ImGui::CalcTextSize(label); draw_list->AddText(ImVec2(center.x - tSize.x/2, pos.y + radius*2 + 2), IM_COL32(150,150,150,255), label); }
    return value_changed;
}

struct KeyMap { int key; int offset; };
std::vector<KeyMap> pcKeys = { {GLFW_KEY_Z, 0}, {GLFW_KEY_S, 1}, {GLFW_KEY_X, 2}, {GLFW_KEY_D, 3}, {GLFW_KEY_C, 4}, {GLFW_KEY_V, 5}, {GLFW_KEY_G, 6}, {GLFW_KEY_B, 7}, {GLFW_KEY_H, 8}, {GLFW_KEY_N, 9}, {GLFW_KEY_J, 10}, {GLFW_KEY_M, 11}, {GLFW_KEY_COMMA, 12}, {GLFW_KEY_L, 13}, {GLFW_KEY_PERIOD, 14}, {GLFW_KEY_SEMICOLON, 15}, {GLFW_KEY_SLASH, 16}, {GLFW_KEY_Q, 12}, {GLFW_KEY_2, 13}, {GLFW_KEY_W, 14}, {GLFW_KEY_3, 15}, {GLFW_KEY_E, 16}, {GLFW_KEY_R, 17}, {GLFW_KEY_5, 18}, {GLFW_KEY_T, 19}, {GLFW_KEY_6, 20}, {GLFW_KEY_Y, 21}, {GLFW_KEY_7, 22}, {GLFW_KEY_U, 23}, {GLFW_KEY_I, 24}, {GLFW_KEY_9, 25}, {GLFW_KEY_O, 26}, {GLFW_KEY_0, 27}, {GLFW_KEY_P, 28} };

bool PianoKey(const char* id, bool isBlack, bool isActive, ImVec2 size) {
    ImVec4 colorNorm = isBlack ? ImVec4(0.12f, 0.12f, 0.12f, 1.0f) : ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    ImVec4 colorActive = ImVec4(0.0f, 0.8f, 0.8f, 1.0f); ImGui::PushStyleColor(ImGuiCol_Button, isActive ? colorActive : colorNorm); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isActive ? colorActive : (isBlack ? ImVec4(0.3f, 0.3f, 0.3f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f))); ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorActive); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); ImGui::Button(id, size); bool isHoveredAndActive = ImGui::IsItemActive(); ImGui::PopStyleColor(4); ImGui::PopStyleVar(); return isHoveredAndActive;
}

void Draw3DTable(ImDrawList* drawList, ImVec2 pos, float viewW, float viewH, const Wavetable3D& table, float currentWtPos, float currentLevel, const std::vector<float>& activeWave, bool enabled) {
    drawList->AddRectFilled(pos, ImVec2(pos.x+viewW, pos.y+viewH), IM_COL32(20,20,25,255)); 
    if (!enabled) { drawList->AddText(ImVec2(pos.x + 10, pos.y + 10), IM_COL32(100,100,100,255), "OFF"); return; }
    int numFrames = (int)table.frames.size(); float plotW = viewW * 0.75f; float plotH = 40.0f; float startX = pos.x + (viewW - plotW) / 2.0f; float startY = pos.y + viewH - 50.0f; float zTiltX = 6.0f; float zTiltY = 6.0f; int step = 16; float activeFramePrecise = currentWtPos * (numFrames - 1); 
    for (int f = numFrames - 1; f >= 0; --f) {
        float zOffsetX = f * zTiltX; float zOffsetY = f * zTiltY; bool isActive = (std::abs(activeFramePrecise - f) <= 0.5f); ImU32 lineColor = isActive ? IM_COL32(0,255,255,255) : IM_COL32(80,100,150,200);
        std::vector<ImVec2> points;
        for (int i = 0; i < TABLE_SIZE; i += step) points.push_back(ImVec2(startX + ((float)i / TABLE_SIZE) * plotW + zOffsetX, startY - (table.frames[f][i] * currentLevel * plotH) - zOffsetY));
        for (size_t i = 0; i < points.size() - 1; ++i) drawList->AddQuadFilled(points[i], points[i+1], ImVec2(points[i+1].x, startY - zOffsetY + plotH), ImVec2(points[i].x, startY - zOffsetY + plotH), IM_COL32(30,35,45,255));
        for (size_t i = 0; i < points.size() - 1; ++i) drawList->AddLine(points[i], points[i+1], lineColor, isActive ? 2.0f : 1.0f);
    }
    float finalZOffsetX = activeFramePrecise * zTiltX; float finalZOffsetY = activeFramePrecise * zTiltY;
    for (int i = 0; i < TABLE_SIZE - step; i += step) {
        ImVec2 p1(startX + ((float)i / TABLE_SIZE) * plotW + finalZOffsetX, startY - (activeWave[i] * currentLevel * plotH) - finalZOffsetY);
        ImVec2 p2(startX + ((float)(i+step) / TABLE_SIZE) * plotW + finalZOffsetX, startY - (activeWave[i+step] * currentLevel * plotH) - finalZOffsetY);
        drawList->AddLine(p1, p2, IM_COL32(255,215,0,255), 2.5f);
    }
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0); glfwWindowHint(GLFW_SAMPLES, 4); 
    GLFWwindow* window = glfwCreateWindow(1280, 950, "Osci: Stable Mod Matrix", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window); glfwSwapInterval(1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui_ImplGlfw_InitForOpenGL(window, true); ImGui_ImplOpenGL3_Init("#version 130");
    ImGui::StyleColorsDark();
    
    for(int i=0; i<MAX_VOICES; ++i) { voicesA.emplace_back(44100.0f); voicesB.emplace_back(44100.0f); }
    UpdateOscillatorsTable(); UpdateEnvelopes(); UpdateFilters();
    for (int i=0; i<512; i++) keysPressedMidi[i] = -1; 

    RefreshMidiPorts();

    WasapiAudioDriver audioDriver;
    if (!audioDriver.init(data_callback)) { std::cerr << "Failed to initialize Windows Audio!" << std::endl; return 1; }
    audioDriver.start();
    
    int mouseHeldNote = -1; 
    std::string currentPresetName = "Init";

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        for (const auto& k : pcKeys) {
            bool isDown = (glfwGetKey(window, k.key) == GLFW_PRESS);
            if (isDown && keysPressedMidi[k.key] == -1) { 
                int midiNote = ((keyboardOctave + 1) * 12) + k.offset; if (midiNote < 0) midiNote = 0; if (midiNote > 127) midiNote = 127;
                keysPressedMidi[k.key] = midiNote; NoteOn(midiNote); 
            } 
            else if (!isDown && keysPressedMidi[k.key] != -1) { 
                NoteOff(keysPressedMidi[k.key]); keysPressedMidi[k.key] = -1; 
            }
        }

        std::vector<std::complex<float>> fftData(FFT_SIZE);
        for(int i=0; i<FFT_SIZE; ++i) {
            int idx = (fftRingPos + i) % FFT_SIZE; float windowFunc = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (FFT_SIZE - 1)));
            fftData[i] = std::complex<float>(fftRingBuffer[idx] * windowFunc, 0.0f);
        }
        computeFFT(fftData); 
        for(int i=0; i<FFT_SIZE/2; ++i) {
            float mag = std::abs(fftData[i]) / (FFT_SIZE / 2.0f); float db = 20.0f * std::log10(std::max(1e-6f, mag));
            float val = (db + 70.0f) / 70.0f; val = std::max(0.0f, std::min(1.0f, val));
            spectrumSmooth[i] = spectrumSmooth[i] * 0.8f + val * 0.2f; 
        }
        
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Synth UI", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

        // --- ГЛОБАЛЬНЫЙ БЛОК ВВЕРХУ ---
        ImGui::BeginGroup(); ImGui::Text("KEYBOARD"); ImGui::PushItemWidth(100); ImGui::DragInt("OCTAVE", &keyboardOctave, 0.1f, 1, 7); ImGui::PopItemWidth(); ImGui::EndGroup();
        
        ImGui::SameLine(0, 30); ImGui::BeginGroup(); ImGui::Text("MIDI IN"); ImGui::PushItemWidth(150);
        if (ImGui::BeginCombo("##MidiIn", selectedMidiPort >= 0 && selectedMidiPort < (int)midiPorts.size() ? midiPorts[selectedMidiPort].c_str() : "None")) {
            if (ImGui::Selectable("None", selectedMidiPort == -1)) { selectedMidiPort = -1; OpenMidiPort(-1); }
            for (int i = 0; i < (int)midiPorts.size(); ++i) { if (ImGui::Selectable(midiPorts[i].c_str(), selectedMidiPort == i)) { selectedMidiPort = i; OpenMidiPort(i); } }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth(); ImGui::EndGroup();

        ImGui::SameLine(0, 30); ImGui::BeginGroup(); ImGui::Text("PRESET: %s", currentPresetName.c_str()); 
        if (ImGui::Button("SAVE")) { std::string path = SaveFileDialog(); if (!path.empty()) { SavePreset(path); currentPresetName = path.substr(path.find_last_of("/\\") + 1); } }
        ImGui::SameLine();
        if (ImGui::Button("LOAD")) { std::string path = OpenFileDialog(); if (!path.empty()) { LoadPreset(path); currentPresetName = path.substr(path.find_last_of("/\\") + 1); } }
        ImGui::EndGroup();

        ImGui::SameLine(0, 30); ImGui::BeginGroup(); ImGui::Text("MASTER"); DrawKnob("Vol", &masterVolumeDb, -36.0f, 12.0f, "%.1f dB", TGT_NONE); ImGui::EndGroup();
        ImGui::Separator();

        if (ImGui::BeginTabBar("SerumTabs")) {
            
            // ===================== ВКЛАДКА OSC =====================
            if (ImGui::BeginTabItem("OSC")) {
                
                // --- ЛЕВАЯ КОЛОНКА (SUB & NOISE) ---
                ImGui::BeginGroup(); 
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255)); 
                    ImGui::BeginChild("LeftTools", ImVec2(180, 260), true);
                        ImGui::Text("SUB OSC"); ImGui::Checkbox("##SubOn", &subEnabled); ImGui::SameLine(); ImGui::PushItemWidth(60); ImGui::DragInt("Oct", &subOctave, 0.1f, -4, 0); ImGui::PopItemWidth(); ImGui::SameLine(); DrawKnob("Lev##Sub", &subLevel, 0.0f, 1.0f, "%.2f", TGT_SUB_LEVEL);
                        ImGui::Separator();
                        ImGui::Text("NOISE"); ImGui::Checkbox("##NoiseOn", &globalNoise.enabled); ImGui::SameLine(0, 5); ImGui::PushItemWidth(80); const char* noiseTypes[] = { "White", "Pink", "Brown" }; ImGui::Combo("##NType", &globalNoise.type, noiseTypes, 3); ImGui::PopItemWidth(); ImGui::SameLine(0, 5); DrawKnob("Lev##Noise", &globalNoise.level, 0.0f, 1.0f, "%.2f", TGT_NOISE_LEVEL);
                        ImGui::Separator();
                        ImGui::Text("VOICING"); ImGui::Checkbox("Legato", &isMonoLegato); ImGui::SameLine(0, 20); DrawKnob("Glide", &glideTime, 0.0f, 1.0f, "%.2fs", TGT_NONE);
                    ImGui::EndChild(); 
                    ImGui::PopStyleColor(); 
                ImGui::EndGroup(); 
                
                ImGui::SameLine(0, 15);

                // --- ПРАВАЯ КОЛОНКА (OSC A, OSC B, ENV, FILTER, LFO) ---
                ImGui::BeginGroup(); 

                    // -- OSC A --
                    ImGui::PushID("OSC_A"); 
                    ImGui::BeginGroup(); 
                        ImGui::Checkbox("OSC A", &oscAEnabled); ImGui::SameLine(); ImGui::PushItemWidth(150);
                        if (ImGui::ArrowButton("##leftWA", ImGuiDir_Left)) { currentTableIndexA--; if (currentTableIndexA < 0) currentTableIndexA = (int)wtManager.tables.size() - 1; UpdateOscillatorsTable(); } ImGui::SameLine(); ImGui::Button(wtManager.tables[currentTableIndexA].name.c_str(), ImVec2(150, 0)); ImGui::SameLine(); if (ImGui::ArrowButton("##rightWA", ImGuiDir_Right)) { currentTableIndexA++; if (currentTableIndexA >= (int)wtManager.tables.size()) currentTableIndexA = 0; UpdateOscillatorsTable(); } ImGui::PopItemWidth();
                        ImGui::SameLine(0, 20); if (DrawKnob("WT POS", &wtPosA, 0.0f, 1.0f, "%.2f", TGT_OSCA_WTPOS)) UpdateOscillatorsTable();
                        ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("PITCH"); ImGui::PushItemWidth(40); ImGui::DragInt("OCT", &oscA_Octave, 0.1f, -4, 4); ImGui::DragInt("SEM", &pitchSemiA, 0.1f, -12, 12); ImGui::PopItemWidth(); ImGui::EndGroup();
                        ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("UNISON"); ImGui::PushItemWidth(40); ImGui::DragInt("##UniA", &unisonVoicesA, 0.2f, 1, 16); ImGui::PopItemWidth(); ImGui::EndGroup();
                        ImGui::SameLine(0, 10); DrawKnob("Detune", &unisonDetuneA, 0.0f, 1.0f, "%.2f", TGT_OSCA_DETUNE); ImGui::SameLine(0, 10); DrawKnob("Blend", &unisonBlendA, 0.0f, 1.0f, "%.2f", TGT_OSCA_BLEND); ImGui::SameLine(0, 10); DrawKnob("Level", &oscALevel, 0.0f, 1.0f, "%.2f", TGT_OSCA_LEVEL);
                    ImGui::EndGroup(); 
                    ImGui::PopID(); 
                    ImGui::Separator();

                    // -- OSC B --
                    ImGui::PushID("OSC_B"); 
                    ImGui::BeginGroup(); 
                        ImGui::Checkbox("OSC B", &oscBEnabled); ImGui::SameLine(); ImGui::PushItemWidth(150);
                        if (ImGui::ArrowButton("##leftWB", ImGuiDir_Left)) { currentTableIndexB--; if (currentTableIndexB < 0) currentTableIndexB = (int)wtManager.tables.size() - 1; UpdateOscillatorsTable(); } ImGui::SameLine(); ImGui::Button(wtManager.tables[currentTableIndexB].name.c_str(), ImVec2(150, 0)); ImGui::SameLine(); if (ImGui::ArrowButton("##rightWB", ImGuiDir_Right)) { currentTableIndexB++; if (currentTableIndexB >= (int)wtManager.tables.size()) currentTableIndexB = 0; UpdateOscillatorsTable(); } ImGui::PopItemWidth();
                        ImGui::SameLine(0, 20); if (DrawKnob("WT POS", &wtPosB, 0.0f, 1.0f, "%.2f", TGT_OSCB_WTPOS)) UpdateOscillatorsTable();
                        ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("PITCH"); ImGui::PushItemWidth(40); ImGui::DragInt("OCT", &oscB_Octave, 0.1f, -4, 4); ImGui::DragInt("SEM", &pitchSemiB, 0.1f, -12, 12); ImGui::PopItemWidth(); ImGui::EndGroup();
                        ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("UNISON"); ImGui::PushItemWidth(40); ImGui::DragInt("##UniB", &unisonVoicesB, 0.2f, 1, 16); ImGui::PopItemWidth(); ImGui::EndGroup();
                        ImGui::SameLine(0, 10); DrawKnob("Detune", &unisonDetuneB, 0.0f, 1.0f, "%.2f", TGT_OSCB_DETUNE); ImGui::SameLine(0, 10); DrawKnob("Blend", &unisonBlendB, 0.0f, 1.0f, "%.2f", TGT_OSCB_BLEND); ImGui::SameLine(0, 10); DrawKnob("Level", &oscBLevel, 0.0f, 1.0f, "%.2f", TGT_OSCB_LEVEL);
                    ImGui::EndGroup(); 
                    ImGui::PopID();
                    ImGui::Separator();

                    // -- ROW: ENV & FILTER --
                    ImGui::BeginGroup(); 
                        
                        // Envelopes
                        ImGui::BeginGroup(); 
                        ImGui::BeginTabBar("EnvTabs");
                        for (int i = 0; i < 3; ++i) {
                            char envName[16]; snprintf(envName, sizeof(envName), i==0 ? "ENV 1 (AMP)" : "ENV %d", i+1);
                            if (ImGui::BeginTabItem(envName)) {
                                uiSelectedModSource = SRC_ENV1 + i;
                                
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
                                ImGui::Button("(+) DRAG MOD", ImVec2(90, 25));
                                ImGui::PopStyleColor();
                                if (ImGui::BeginDragDropSource()) {
                                    int src = SRC_ENV1 + i;
                                    ImGui::SetDragDropPayload("MOD_SRC", &src, sizeof(int));
                                    ImGui::Text("Assign ENV %d", i+1);
                                    ImGui::EndDragDropSource();
                                }

                                ImGui::SameLine(0, 20); bool envC = false;
                                if (DrawKnob("A", &envA[i], 0.001f, 5.0f, "%.2fs", TGT_NONE)) envC=true; ImGui::SameLine(0, 10); if (DrawKnob("D", &envD[i], 0.001f, 5.0f, "%.2fs", TGT_NONE)) envC=true; ImGui::SameLine(0, 10); if (DrawKnob("S", &envS[i], 0.0f, 1.0f, "%.2f", TGT_NONE)) envC=true; ImGui::SameLine(0, 10); if (DrawKnob("R", &envR[i], 0.001f, 5.0f, "%.2fs", TGT_NONE)) envC=true; 
                                if (envC) UpdateEnvelopes();
                                
                                ImVec2 envPos = ImGui::GetCursorScreenPos(); ImVec2 envSize(260.0f, 60.0f); ImGui::InvisibleButton("##EnvGraph", envSize); ImDrawList* ed = ImGui::GetWindowDrawList(); ed->AddRectFilled(envPos, ImVec2(envPos.x + envSize.x, envPos.y + envSize.y), IM_COL32(30, 30, 35, 255)); 
                                float tt = 10.0f; float pxA = (envA[i] / tt) * envSize.x * 2.0f; float pxD = (envD[i] / tt) * envSize.x * 2.0f; float pxS = 40.0f; float pxR = (envR[i] / tt) * envSize.x * 2.0f; 
                                ImVec2 p0(envPos.x, envPos.y + envSize.y), p1(p0.x + pxA, envPos.y), p2(p1.x + pxD, envPos.y + envSize.y - (envS[i] * envSize.y)), p3(p2.x + pxS, p2.y), p4(p3.x + pxR, envPos.y + envSize.y); 
                                ed->AddLine(p0, p1, IM_COL32(0, 200, 255, 255), 2.0f); ed->AddLine(p1, p2, IM_COL32(0, 200, 255, 255), 2.0f); ed->AddLine(p2, p3, IM_COL32(0, 200, 255, 255), 2.0f); ed->AddLine(p3, p4, IM_COL32(0, 200, 255, 255), 2.0f); ed->AddCircleFilled(p1, 4.0f, IM_COL32(255, 255, 255, 255)); ed->AddCircleFilled(p2, 4.0f, IM_COL32(255, 255, 255, 255)); ed->AddCircleFilled(p4, 4.0f, IM_COL32(255, 255, 255, 255));
                                
                                float lvl = 0; int state = 0;
                                if (i == 0) { float maxLvl = -1.0f; for (int v = 0; v < MAX_VOICES; ++v) { if (voicesA[v].isActive() && voicesA[v].env.currentLevel > maxLvl) { maxLvl = voicesA[v].env.currentLevel; lvl = maxLvl; state = voicesA[v].env.state; } } }
                                else { lvl = auxEnvs[i-1].val; state = auxEnvs[i-1].state; }
                                
                                if (state != 0) {
                                    float dotY = envPos.y + envSize.y - (lvl * envSize.y); float dotX = envPos.x;
                                    if (state == 1) dotX = p0.x + pxA * lvl; else if (state == 2) { float r = 1.0f - envS[i]; dotX = p1.x + pxD * ((r > 0.001f) ? ((1.0f - lvl)/r) : 1.0f); } else if (state == 3) dotX = p2.x + pxS * 0.5f; else if (state == 4) { float t = (envS[i] > 0.001f) ? (1.0f - (lvl / envS[i])) : 1.0f; dotX = p3.x + pxR * std::max(0.0f, std::min(1.0f, t)); }
                                    ed->AddCircleFilled(ImVec2(dotX, dotY), 4.5f, IM_COL32(255, 215, 0, 255));
                                }
                                ImGui::EndTabItem();
                            }
                        }
                        ImGui::EndTabBar();
                        ImGui::EndGroup(); 

                        ImGui::SameLine(0, 40); 
                        
                        // Filter
                        ImGui::PushID("FILTER1");
                        ImGui::BeginGroup(); 
                            ImGui::Text("FILTER"); 
                            ImGui::BeginGroup(); 
                                bool filterChanged = false;
                                if (ImGui::Checkbox("##FiltEnable", &filterEnabled)) filterChanged = true; ImGui::SameLine(); ImGui::PushItemWidth(150);
                                const char* filterTypes[] = { "Low Pass 12dB", "High Pass 12dB", "Band Pass 12dB" };
                                if (ImGui::Combo("##FiltType", &filterType, filterTypes, 3)) filterChanged = true; ImGui::PopItemWidth();
                                ImGui::SameLine(0, 20); if (DrawKnob("Cutoff", &filterCutoff, 20.0f, 20000.0f, "%.0f Hz", TGT_FILT_CUTOFF, true)) filterChanged = true;
                                ImGui::SameLine(0, 10); if (DrawKnob("Res", &filterResonance, 0.1f, 10.0f, "%.2f Q", TGT_FILT_RES)) filterChanged = true;
                                if (filterChanged) UpdateFilters(); 
                            ImGui::EndGroup(); 

                            ImGui::SameLine(0, 20); ImVec2 filtPos = ImGui::GetCursorScreenPos(); ImVec2 filtSize(250.0f, 80.0f); ImGui::InvisibleButton("##FiltGraph", filtSize); ImDrawList* fd = ImGui::GetWindowDrawList();
                            fd->AddRectFilled(filtPos, ImVec2(filtPos.x + filtSize.x, filtPos.y + filtSize.y), IM_COL32(30, 30, 35, 255));
                            if (filterEnabled) {
                                std::vector<ImVec2> pts; int numPoints = (int)filtSize.x;
                                for (int i = 0; i < numPoints; ++i) { float freq = 20.0f * std::pow(1000.0f, (float)i / (numPoints - 1.0f)); float magDb = 20.0f * std::log10(std::max(0.0001f, voicesA[0].filter.getMagnitude(freq, 44100.0f))); pts.push_back(ImVec2(filtPos.x + i, filtPos.y + std::max(0.0f, std::min(1.0f, 1.0f - (magDb + 24.0f) / 42.0f)) * filtSize.y)); }
                                for (size_t i = 0; i < pts.size() - 1; ++i) fd->AddLine(pts[i], pts[i+1], IM_COL32(0, 255, 100, 255), 2.0f);
                                
                                float liveCutoff = std::max(20.0f, std::min(20000.0f, filterCutoff * std::pow(2.0f, GetModSum(TGT_FILT_CUTOFF) * 5.0f)));
                                float xCut = filtPos.x + (std::log(liveCutoff / 20.0f) / std::log(1000.0f)) * filtSize.x;
                                if (xCut >= filtPos.x && xCut <= filtPos.x + filtSize.x) { fd->AddLine(ImVec2(xCut, filtPos.y), ImVec2(xCut, filtPos.y + filtSize.y), IM_COL32(255, 255, 255, 50), 1.0f); float yNorm = 1.0f - (20.0f * std::log10(std::max(0.0001f, voicesA[0].filter.getMagnitude(liveCutoff, 44100.0f))) + 24.0f) / 42.0f; fd->AddCircleFilled(ImVec2(xCut, filtPos.y + std::max(0.0f, std::min(1.0f, yNorm)) * filtSize.y), 4.0f, IM_COL32(255, 255, 255, 255)); }
                            } else { fd->AddLine(ImVec2(filtPos.x, filtPos.y + (1.0f - 24.0f / 42.0f) * filtSize.y), ImVec2(filtPos.x + filtSize.x, filtPos.y + (1.0f - 24.0f / 42.0f) * filtSize.y), IM_COL32(100, 100, 100, 255), 2.0f); }
                        ImGui::EndGroup(); 
                        ImGui::PopID(); 

                    ImGui::EndGroup(); // END ROW: ENV & FILTER
                    ImGui::Separator();

                    // -- ROW: LFO & SPECTRUM --
                    ImGui::BeginGroup(); 
                        
                        // LFOs
                        ImGui::BeginGroup(); 
                        ImGui::BeginTabBar("LfoTabs");
                        for (int i = 0; i < 2; ++i) {
                            char lfoName[16]; snprintf(lfoName, sizeof(lfoName), "LFO %d", i+1);
                            if (ImGui::BeginTabItem(lfoName)) {
                                uiSelectedModSource = SRC_LFO1 + i;
                                
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.8f, 1.0f));
                                ImGui::Button("(+) DRAG MOD", ImVec2(90, 25));
                                ImGui::PopStyleColor();
                                if (ImGui::BeginDragDropSource()) {
                                    int src = SRC_LFO1 + i;
                                    ImGui::SetDragDropPayload("MOD_SRC", &src, sizeof(int));
                                    ImGui::Text("Assign LFO %d", i+1);
                                    ImGui::EndDragDropSource();
                                }

                                ImGui::SameLine(0, 20); ImGui::PushItemWidth(80);
                                const char* lfoShapes[] = { "Sine", "Triangle", "Saw", "Square" }; ImGui::Combo("Shape", &globalLFOs[i].shape, lfoShapes, 4); ImGui::SameLine();
                                const char* syncModes[] = { "Hz", "BPM" }; ImGui::Combo("Sync", &lfoSyncMode[i], syncModes, 2);
                                if (lfoSyncMode[i] == 1) { ImGui::SameLine(); ImGui::DragFloat("BPM", &globalBpm, 1.0f, 20.0f, 300.0f, "%.1f"); ImGui::SameLine(); const char* rates[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }; ImGui::Combo("Rate", &lfoRateBPMIndex[i], rates, 6); } 
                                else { ImGui::SameLine(); ImGui::SliderFloat("Rate", &lfoRateHz[i], 0.05f, 20.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic); }
                                ImGui::SameLine(); ImGui::ProgressBar((globalLFOs[i].currentValue + 1.0f) / 2.0f, ImVec2(100, 20), "Phase");
                                ImGui::PopItemWidth(); ImGui::EndTabItem();
                            }
                        }
                        ImGui::EndTabBar();
                        ImGui::EndGroup(); 

                        ImGui::SameLine(0, 30); 
                        
                        // Spectrum
                        ImGui::BeginGroup(); 
                        ImGui::Text("EQ SPECTRUM"); ImVec2 specPos = ImGui::GetCursorScreenPos(); float specW = ImGui::GetContentRegionAvail().x - 10.0f; float specH = 80.0f;
                        ImGui::InvisibleButton("##Spectrum", ImVec2(specW, specH)); ImDrawList* specDraw = ImGui::GetWindowDrawList(); specDraw->AddRectFilled(specPos, ImVec2(specPos.x + specW, specPos.y + specH), IM_COL32(20, 20, 25, 255));
                        for(int x = 0; x < (int)specW - 1; x += 3) {
                            float t = (float)x / specW; float freq = 20.0f * std::pow(1000.0f, t); float binF = freq * (FFT_SIZE / 44100.0f); int bin = std::max(0, std::min((int)binF, FFT_SIZE/2 - 1)); float val = spectrumSmooth[bin];
                            specDraw->AddLine(ImVec2(specPos.x + x, specPos.y + specH), ImVec2(specPos.x + x, specPos.y + specH - val * specH), IM_COL32(255, 100, 200, 200), 2.0f); 
                        }
                        ImGui::EndGroup(); 

                    ImGui::EndGroup(); // END ROW: LFO & SPECTRUM
                    ImGui::Separator();

                    // -- ROW: 3D RENDER --
                    ImVec2 pos3d=ImGui::GetCursorScreenPos(); float totalW=ImGui::GetContentRegionAvail().x; float viewH3d=160.0f; ImGui::InvisibleButton("##3DViewSplit", ImVec2(totalW, viewH3d)); ImDrawList* drawList = ImGui::GetWindowDrawList(); 
                    Draw3DTable(drawList, pos3d, totalW/2.0f - 5.0f, viewH3d, wtManager.tables[currentTableIndexA], std::max(0.0f, std::min(1.0f, wtPosA + GetModSum(TGT_OSCA_WTPOS))), std::max(0.0f, std::min(1.0f, oscALevel + GetModSum(TGT_OSCA_LEVEL))), voicesA[0].getWavetableData(), oscAEnabled);
                    Draw3DTable(drawList, ImVec2(pos3d.x + totalW/2.0f + 5.0f, pos3d.y), totalW/2.0f - 5.0f, viewH3d, wtManager.tables[currentTableIndexB], std::max(0.0f, std::min(1.0f, wtPosB + GetModSum(TGT_OSCB_WTPOS))), std::max(0.0f, std::min(1.0f, oscBLevel + GetModSum(TGT_OSCB_LEVEL))), voicesB[0].getWavetableData(), oscBEnabled);

                ImGui::EndGroup(); // END RIGHT COLUMN

                ImGui::EndTabItem();
            }

            // ===================== ВКЛАДКА FX =====================
            if (ImGui::BeginTabItem("FX")) {
                ImGui::Dummy(ImVec2(0.0f, 10.0f)); ImGui::PushID("FX_DIST"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255)); ImGui::BeginChild("DistortionPanel", ImVec2(0, 100), true); ImGui::Checkbox("DISTORTION", &globalDistortion.enabled); ImGui::SameLine(150); DrawKnob("Drive", &globalDistortion.drive, 0.0f, 1.0f, "%.2f", TGT_DIST_DRIVE); ImGui::SameLine(250); DrawKnob("Mix", &globalDistortion.mix, 0.0f, 1.0f, "%.2f", TGT_DIST_MIX); ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();
                ImGui::Dummy(ImVec2(0.0f, 10.0f)); ImGui::PushID("FX_DELAY"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255)); ImGui::BeginChild("DelayPanel", ImVec2(0, 100), true); ImGui::Checkbox("DELAY", &globalDelay.enabled); ImGui::SameLine(150); DrawKnob("Time", &globalDelay.time, 0.01f, 1.5f, "%.2fs", TGT_DEL_TIME); ImGui::SameLine(250); DrawKnob("Fback", &globalDelay.feedback, 0.0f, 0.95f, "%.2f", TGT_DEL_FB); ImGui::SameLine(350); DrawKnob("Mix", &globalDelay.mix, 0.0f, 1.0f, "%.2f", TGT_DEL_MIX); ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();
                ImGui::Dummy(ImVec2(0.0f, 10.0f)); ImGui::PushID("FX_REV"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255)); ImGui::BeginChild("ReverbPanel", ImVec2(0, 100), true); ImGui::Checkbox("REVERB", &globalReverb.enabled); ImGui::SameLine(150); DrawKnob("Size", &globalReverb.roomSize, 0.0f, 0.98f, "%.2f", TGT_REV_SIZE); ImGui::SameLine(250); DrawKnob("Damp", &globalReverb.damping, 0.0f, 1.0f, "%.2f", TGT_REV_DAMP); ImGui::SameLine(350); DrawKnob("Mix", &globalReverb.mix, 0.0f, 1.0f, "%.2f", TGT_REV_MIX); ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();
                ImGui::Dummy(ImVec2(0.0f, 60.0f)); ImGui::EndTabItem();
            }

            // ===================== ВКЛАДКА MOD MATRIX =====================
            if (ImGui::BeginTabItem("MOD MATRIX")) {
                ImGui::Columns(4, "modmatrix_cols"); ImGui::Separator();
                ImGui::Text("SOURCE"); ImGui::NextColumn(); ImGui::Text("AMOUNT"); ImGui::NextColumn(); ImGui::Text("DESTINATION"); ImGui::NextColumn(); ImGui::Text("ACTION"); ImGui::NextColumn(); ImGui::Separator();

                const char* srcNames[] = { "None", "LFO 1", "LFO 2", "ENV 1", "ENV 2", "ENV 3" };
                const char* tgtNames[] = { "None", "Osc A WT Pos", "Osc A Pitch", "Osc A Level", "Osc A Detune", "Osc A Blend", "Osc B WT Pos", "Osc B Pitch", "Osc B Level", "Osc B Detune", "Osc B Blend", "Filter Cutoff", "Filter Res", "Sub Level", "Noise Level", "Dist Drive", "Dist Mix", "Delay Time", "Delay FB", "Delay Mix", "Reverb Size", "Reverb Damp", "Reverb Mix" };

                for (int i = 0; i < (int)modMatrix.size(); ++i) {
                    ImGui::PushID(i);
                    ImGui::Text("%s", srcNames[modMatrix[i].source]); ImGui::NextColumn();
                    ImGui::SliderFloat("##amt", &modMatrix[i].amount, -1.0f, 1.0f, "%.2f"); ImGui::NextColumn();
                    ImGui::Text("%s", tgtNames[modMatrix[i].target]); ImGui::NextColumn();
                    if (ImGui::Button("REMOVE", ImVec2(80, 0))) { modMatrix.erase(modMatrix.begin() + i); i--; } ImGui::NextColumn();
                    ImGui::PopID();
                }
                ImGui::Columns(1); ImGui::Separator();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        ImGui::Separator();

        // === ОСЦИЛЛОГРАФ ===
        ImVec2 scopePos = ImGui::GetCursorScreenPos(); float scopeW = ImGui::GetContentRegionAvail().x; float scopeH = 60.0f; ImGui::InvisibleButton("##MainScope", ImVec2(scopeW, scopeH)); ImDrawList* scopeDraw = ImGui::GetWindowDrawList();
        scopeDraw->AddRectFilled(scopePos, ImVec2(scopePos.x + scopeW, scopePos.y + scopeH), IM_COL32(15, 15, 20, 255)); float midY = scopePos.y + scopeH * 0.5f; scopeDraw->AddLine(ImVec2(scopePos.x, midY), ImVec2(scopePos.x + scopeW, midY), IM_COL32(40, 40, 50, 255), 1.0f); 
        for (int i = 0; i < SCOPE_SIZE - 1; ++i) {
            float x1 = scopePos.x + ((float)i / (SCOPE_SIZE - 1)) * scopeW; float x2 = scopePos.x + ((float)(i + 1) / (SCOPE_SIZE - 1)) * scopeW;
            float y1 = midY - std::max(-1.0f, std::min(1.0f, scopeBuffer[i])) * (scopeH * 0.45f); float y2 = midY - std::max(-1.0f, std::min(1.0f, scopeBuffer[i+1])) * (scopeH * 0.45f);
            scopeDraw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 255, 150, 255), 1.5f);
        }
        ImGui::Separator();

        // === ПИАНИНО ===
        ImGui::BeginChild("PianoScroll",ImVec2(0,200),true,ImGuiWindowFlags_HorizontalScrollbar);float whiteW=36.0f,whiteH=150.0f,blackW=22.0f,blackH=90.0f,spacing=1.0f;int startOct=2,endOct=7,currentMouseNote=-1;ImVec2 startPosP=ImGui::GetCursorPos();int whiteIndex=0;for(int oct=startOct;oct<=endOct;++oct){for(int note=0;note<12;++note){if(note==1||note==3||note==6||note==8||note==10)continue;int midi=(oct+1)*12+note;ImGui::SetCursorPos(ImVec2(startPosP.x+whiteIndex*(whiteW+spacing),startPosP.y));ImGui::PushID(midi);if(PianoKey("##w",false,noteState[midi],ImVec2(whiteW,whiteH)))currentMouseNote=midi;ImGui::PopID();if(note==0){ImGui::SetCursorPos(ImVec2(startPosP.x+whiteIndex*(whiteW+spacing)+8,startPosP.y+whiteH-20));ImGui::TextColored(ImVec4(0.2f,0.2f,0.2f,1.0f),"C%d",oct);}whiteIndex++;}}whiteIndex=0;for(int oct=startOct;oct<=endOct;++oct){for(int note=0;note<12;++note){bool isBlack=(note==1||note==3||note==6||note==8||note==10);if(!isBlack){whiteIndex++;continue;}int midi=(oct+1)*12+note;float offsetX=startPosP.x+whiteIndex*(whiteW+spacing)-(blackW/2.0f)-(spacing/2.0f);ImGui::SetCursorPos(ImVec2(offsetX,startPosP.y));ImGui::PushID(midi);if(PianoKey("##b",true,noteState[midi],ImVec2(blackW,blackH)))currentMouseNote=midi;ImGui::PopID();}}ImGui::SetCursorPos(ImVec2(startPosP.x,startPosP.y+whiteH+10));ImGui::EndChild();if(mouseHeldNote!=-1&&mouseHeldNote!=currentMouseNote){if(!keysPressedMidi[mouseHeldNote])NoteOff(mouseHeldNote);mouseHeldNote=-1;}if(currentMouseNote!=-1&&currentMouseNote!=mouseHeldNote){NoteOn(currentMouseNote);mouseHeldNote=currentMouseNote;}

        ImGui::End(); ImGui::Render(); int w, h; glfwGetFramebufferSize(window, &w, &h); glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); glfwSwapBuffers(window);
    }
    
    audioDriver.stop(); if (hMidiIn) { midiInStop(hMidiIn); midiInClose(hMidiIn); } ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate(); return 0;
}