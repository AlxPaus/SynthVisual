#pragma once
#include <vector>
#include <mutex>
#include <string>
#include "osc.h"
#include "dsp.h"

const int MAX_VOICES = 128;
const int SCOPE_SIZE = 512;
const int FFT_SIZE = 2048;

inline std::mutex audioMutex;

inline std::vector<WavetableOscillator> voicesA;
inline std::vector<WavetableOscillator> voicesB;
inline WavetableManager wtManager;

inline int keyboardOctave = 4;
inline bool isMonoLegato = false;
inline float glideTime = 0.05f; 
inline std::vector<int> heldNotesStack;
inline float currentGlideFreq = 0.0f;
inline float targetGlideFreq = 0.0f;
inline float globalLastPolyFreq = 440.0f;

inline bool subEnabled = false; inline int subOctave = -1; inline float subLevel = 0.5f; inline float subPhase = 0.0f;
inline NoiseGenerator globalNoise;

inline bool oscAEnabled = true; inline int currentTableIndexA = 0; inline int oscA_Octave = 0; inline int pitchSemiA = 0; inline float wtPosA = 0.0f; inline int unisonVoicesA = 1; inline float unisonDetuneA = 0.15f; inline float unisonBlendA = 0.75f; inline float oscALevel = 0.75f; 
inline bool oscBEnabled = false; inline int currentTableIndexB = 0; inline int oscB_Octave = 0; inline int pitchSemiB = 0; inline float wtPosB = 0.0f; inline int unisonVoicesB = 1; inline float unisonDetuneB = 0.15f; inline float unisonBlendB = 0.75f; inline float oscBLevel = 0.75f; 

inline float globalPhase = 0.0f; inline float globalPhaseRand = 100.0f; inline float masterVolumeDb = 0.0f; 
inline bool filterEnabled = false; inline int filterType = 0; inline float filterCutoff = 2000.0f; inline float filterResonance = 1.0f; 

enum ModSource { SRC_NONE = 0, SRC_LFO1, SRC_LFO2, SRC_ENV1, SRC_ENV2, SRC_ENV3 };
enum ModTarget { TGT_NONE = 0, TGT_OSCA_WTPOS, TGT_OSCA_PITCH, TGT_OSCA_LEVEL, TGT_OSCA_DETUNE, TGT_OSCA_BLEND, TGT_OSCB_WTPOS, TGT_OSCB_PITCH, TGT_OSCB_LEVEL, TGT_OSCB_DETUNE, TGT_OSCB_BLEND, TGT_FILT_CUTOFF, TGT_FILT_RES, TGT_SUB_LEVEL, TGT_NOISE_LEVEL, TGT_DIST_DRIVE, TGT_DIST_MIX, TGT_DEL_TIME, TGT_DEL_FB, TGT_DEL_MIX, TGT_REV_SIZE, TGT_REV_DAMP, TGT_REV_MIX };

struct ModRouting { int source; int target; float amount; };
inline std::vector<ModRouting> modMatrix;
inline int uiSelectedModSource = SRC_LFO1; 

inline float envA[3] = {0.05f, 0.05f, 0.05f}; inline float envD[3] = {0.50f, 0.50f, 0.50f}; inline float envS[3] = {0.70f, 0.70f, 0.70f}; inline float envR[3] = {0.30f, 0.30f, 0.30f};
inline GlobalEnv auxEnvs[2];
inline AdvancedLFO globalLFOs[2];

inline int lfoSyncMode[2] = {0, 0}; inline float globalBpm = 120.0f; inline float lfoRateHz[2] = {1.0f, 1.0f}; inline int lfoRateBPMIndex[2] = {2, 2};
inline bool noteState[512] = { false }; inline int keysPressedMidi[512]; 

inline DistortionFX globalDistortion; inline DelayFX globalDelay; inline ReverbFX globalReverb;

inline std::vector<float> scopeBuffer(SCOPE_SIZE, 0.0f); inline int scopeIndex = 0; inline bool scopeTriggered = false; inline float lastScopeSample = 0.0f;
inline std::vector<float> fftRingBuffer(FFT_SIZE, 0.0f); inline int fftRingPos = 0; inline std::vector<float> spectrumSmooth(FFT_SIZE / 2, 0.0f);