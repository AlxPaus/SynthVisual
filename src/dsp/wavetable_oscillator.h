#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "constants.h"
#include "adsr.h"
#include "biquad_filter.h"
#include "wavetable.h"

class WavetableOscillator {
public:
    ADSR env;
    BiquadFilter filter;

    WavetableOscillator(float sampleRate)
        : sampleRate(sampleRate), phase(0.0f), increment(0.0f), frequency(440.0f),
          currentMidiNote(-1), pan(0.0f), wtPosNormalized(0.0f), currentTable(nullptr)
    {
        interpolatedWave.resize(TABLE_SIZE);
    }

    const std::vector<float>& getWavetableData() const { return interpolatedWave; }

    void setWavetable(const Wavetable3D* table) {
        currentTable = table;
        updateInterpolatedWave();
    }

    void setWTPos(float pos) {
        wtPosNormalized = pos;
        updateInterpolatedWave();
    }

    void updateInterpolatedWave() {
        if (!currentTable || currentTable->frames.empty()) return;
        int numFrames = (int)currentTable->frames.size();
        float preciseFrame = wtPosNormalized * (numFrames - 1);
        int frame1 = std::min((int)std::floor(preciseFrame), numFrames - 1);
        int frame2 = std::min((int)std::ceil(preciseFrame),  numFrames - 1);
        float mix = preciseFrame - frame1;
        for (int i = 0; i < TABLE_SIZE; ++i)
            interpolatedWave[i] = currentTable->frames[frame1][i] * (1.0f - mix)
                                + currentTable->frames[frame2][i] * mix;
    }

    void setPan(float p) { pan = p; }
    float getPan() const { return pan; }
    void setPhase(float p) { phase = p; }

    void setFrequency(float freq) {
        frequency = freq;
        increment = (TABLE_SIZE * frequency) / sampleRate;
    }

    void noteOn(int midiNote) {
        currentMidiNote = midiNote;
        setFrequency(440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f));
        env.noteOn();
        filter.reset();
    }

    void noteOff() { env.noteOff(); }
    bool isActive() const { return env.isActive(); }
    int getNote() const { return currentMidiNote; }

    float getSample() {
        if (!isActive() || !currentTable) return 0.0f;
        int idx0 = (int)phase;
        int idx1 = (idx0 + 1) % TABLE_SIZE;
        float frac = phase - (float)idx0;
        float raw = interpolatedWave[idx0] + frac * (interpolatedWave[idx1] - interpolatedWave[idx0]);
        phase += increment;
        while (phase >= TABLE_SIZE) phase -= TABLE_SIZE;
        return filter.process(raw * env.process());
    }

private:
    std::vector<float> interpolatedWave;
    const Wavetable3D* currentTable;
    float sampleRate, phase, increment, frequency;
    int   currentMidiNote;
    float pan, wtPosNormalized;
};
