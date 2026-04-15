#pragma once

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
    int   syncMode     = 0;
    float rateHz       = 1.0f;
    int   bpmRateIndex = 2;
};
