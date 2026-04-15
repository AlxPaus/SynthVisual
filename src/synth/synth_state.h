#pragma once
#include <vector>
#include <string>
#include "dsp/dsp.h"
#include "synth/synth_types.h"

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
    int   scopeIndex      = 0;
    bool  scopeTriggered  = false;
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
