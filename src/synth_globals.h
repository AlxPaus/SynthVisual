#pragma once
#include <vector>
#include <mutex>
#include <string>
#include "osc.h"
#include "dsp.h"

constexpr int   MAX_VOICES  = 128;
constexpr int   SCOPE_SIZE  = 512;
constexpr int   FFT_SIZE    = 2048;
constexpr float kSampleRate = 44100.0f;

// === MOD MATRIX TYPES ===

enum ModSource {
    SRC_NONE = 0,
    SRC_LFO1, SRC_LFO2,
    SRC_ENV1, SRC_ENV2, SRC_ENV3
};

enum ModTarget {
    TGT_NONE = 0,
    TGT_OSCA_WTPOS, TGT_OSCA_PITCH, TGT_OSCA_LEVEL, TGT_OSCA_DETUNE, TGT_OSCA_BLEND,
    TGT_OSCB_WTPOS, TGT_OSCB_PITCH, TGT_OSCB_LEVEL, TGT_OSCB_DETUNE, TGT_OSCB_BLEND,
    TGT_FILT_CUTOFF, TGT_FILT_RES,
    TGT_SUB_LEVEL, TGT_NOISE_LEVEL,
    TGT_DIST_DRIVE, TGT_DIST_MIX,
    TGT_DEL_TIME, TGT_DEL_FB, TGT_DEL_MIX,
    TGT_REV_SIZE, TGT_REV_DAMP, TGT_REV_MIX
};

struct ModRouting {
    int   source;
    int   target;
    float amount;
};

// === SUB-STRUCTS (group related parameters) ===

struct OscConfig {
    bool  enabled      = false;
    int   tableIndex   = 0;
    int   octave       = 0;
    int   semi         = 0;
    float wtPos        = 0.0f;
    int   unisonVoices = 1;
    float unisonDetune = 0.15f;
    float unisonBlend  = 0.75f;
    float level        = 0.75f;
};

struct EnvParams {
    float attack  = 0.05f;
    float decay   = 0.50f;
    float sustain = 0.70f;
    float release = 0.30f;
};

struct FilterConfig {
    bool  enabled   = false;
    int   type      = 0;
    float cutoff    = 2000.0f;
    float resonance = 1.0f;
};

struct LfoConfig {
    int   syncMode    = 0;
    float rateHz      = 1.0f;
    int   bpmRateIndex = 2;
};

// === MAIN SYNTH STATE ===

struct SynthState {
    std::vector<WavetableOscillator> voicesA;
    std::vector<WavetableOscillator> voicesB;
    WavetableManager wavetables;

    OscConfig    oscA;        // oscA.enabled = true in constructor
    OscConfig    oscB;
    FilterConfig filter;

    EnvParams  envParams[3]; // [0]=AMP envelope, [1]=ENV2, [2]=ENV3
    GlobalEnv  auxEnvs[2];  // run-time state for ENV2 and ENV3

    LfoConfig   lfoConfig[2];
    AdvancedLFO lfos[2];
    float       bpm = 120.0f;

    std::vector<ModRouting> modMatrix;
    int uiModSource = SRC_LFO1;

    bool  subEnabled = false;
    int   subOctave  = -1;
    float subLevel   = 0.5f;
    float subPhase   = 0.0f;
    NoiseGenerator noise;

    DistortionFX distortion;
    DelayFX      delay;
    ReverbFX     reverb;

    bool  monoLegato       = false;
    float glideTime        = 0.05f;
    std::vector<int> heldNotes;
    float currentGlideFreq = 0.0f;
    float targetGlideFreq  = 0.0f;
    float lastPolyFreq     = 440.0f;

    float masterVolumeDb = 0.0f;
    float startPhase     = 0.0f;
    float startPhaseRand = 100.0f;

    // noteActive[midi_note] — is this note currently sounding
    bool noteActive[128] = {};
    // pcKeyNote[glfw_key]  — midi note held by this PC key (-1 if released)
    int  pcKeyNote[512]  = {};
    // pcNoteHeld[midi_note] — is this note held by the PC keyboard
    bool pcNoteHeld[128] = {};
    int  keyboardOctave  = 4;

    std::vector<float> scopeBuffer;
    int   scopeIndex     = 0;
    bool  scopeTriggered = false;
    float lastScopeSample = 0.0f;

    std::vector<float> fftRingBuffer;
    int   fftRingPos = 0;

    std::vector<float> spectrumSmooth;

    SynthState() {
        oscA.enabled = true;
        scopeBuffer.assign(SCOPE_SIZE, 0.0f);
        fftRingBuffer.assign(FFT_SIZE, 0.0f);
        spectrumSmooth.assign(FFT_SIZE / 2, 0.0f);
        std::fill(std::begin(pcKeyNote), std::end(pcKeyNote), -1);
    }
};

inline SynthState g_synth;
inline std::mutex audioMutex;
