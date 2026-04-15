#pragma once
#include <algorithm>

enum EnvState { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

class ADSR {
public:
    float attackTime, decayTime, sustainLevel, releaseTime;
    float sampleRate, currentLevel, releaseStep;
    EnvState state;

    ADSR(float sr = 44100.0f)
        : sampleRate(sr), state(ENV_IDLE), currentLevel(0.0f), releaseStep(0.0f)
    {
        setParameters(0.05f, 0.5f, 0.7f, 0.3f);
    }

    void setParameters(float a, float d, float s, float r) {
        attackTime   = std::max(0.001f, a);
        decayTime    = std::max(0.001f, d);
        sustainLevel = s;
        releaseTime  = std::max(0.001f, r);
    }

    void noteOn()  { state = ENV_ATTACK; }
    void noteOff() { state = ENV_RELEASE; releaseStep = currentLevel / (releaseTime * sampleRate); }

    float process() {
        switch (state) {
            case ENV_IDLE:
                currentLevel = 0.0f;
                break;
            case ENV_ATTACK:
                currentLevel += 1.0f / (attackTime * sampleRate);
                if (currentLevel >= 1.0f) { currentLevel = 1.0f; state = ENV_DECAY; }
                break;
            case ENV_DECAY:
                currentLevel -= (1.0f - sustainLevel) / (decayTime * sampleRate);
                if (currentLevel <= sustainLevel) { currentLevel = sustainLevel; state = ENV_SUSTAIN; }
                break;
            case ENV_SUSTAIN:
                currentLevel = sustainLevel;
                break;
            case ENV_RELEASE:
                currentLevel -= releaseStep;
                if (currentLevel <= 0.0f) { currentLevel = 0.0f; state = ENV_IDLE; }
                break;
        }
        return currentLevel;
    }

    bool isActive() const { return state != ENV_IDLE; }
};
