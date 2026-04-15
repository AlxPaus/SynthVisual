#pragma once
#include <vector>
#include <string>
#include <cmath>
#include "constants.h"

struct Wavetable3D {
    std::string name;
    std::vector<std::vector<float>> frames;
};

class WavetableManager {
public:
    std::vector<Wavetable3D> tables;

    WavetableManager() {
        generateBasicShapes();
        generatePWMSweep();
        generateFMWub();
        generateHarmonicGrowl();
    }

private:
    void generateBasicShapes() {
        Wavetable3D wt;
        wt.name = "Basic Shapes";
        wt.frames.resize(4, std::vector<float>(TABLE_SIZE));
        for (int i = 0; i < TABLE_SIZE; ++i) {
            float t = (float)i / TABLE_SIZE;
            wt.frames[0][i] = std::sin(t * 2.0f * PI);
            wt.frames[1][i] = 2.0f * std::abs(2.0f * t - 1.0f) - 1.0f;
            wt.frames[2][i] = 1.0f - 2.0f * t;
            wt.frames[3][i] = (i < TABLE_SIZE / 2) ? 1.0f : -1.0f;
        }
        tables.push_back(wt);
    }

    void generatePWMSweep() {
        Wavetable3D wt;
        wt.name = "PWM Sweep";
        wt.frames.resize(16, std::vector<float>(TABLE_SIZE));
        for (int f = 0; f < 16; ++f) {
            float pw = 0.5f - (0.45f * ((float)f / 15.0f));
            for (int i = 0; i < TABLE_SIZE; ++i)
                wt.frames[f][i] = ((float)i / TABLE_SIZE < pw) ? 1.0f : -1.0f;
        }
        tables.push_back(wt);
    }

    void generateFMWub() {
        Wavetable3D wt;
        wt.name = "FM Wub";
        wt.frames.resize(16, std::vector<float>(TABLE_SIZE));
        for (int f = 0; f < 16; ++f) {
            float mod = 5.0f * ((float)f / 15.0f);
            for (int i = 0; i < TABLE_SIZE; ++i) {
                float t = (float)i / TABLE_SIZE * 2.0f * PI;
                wt.frames[f][i] = std::sin(t + mod * std::sin(t));
            }
        }
        tables.push_back(wt);
    }

    void generateHarmonicGrowl() {
        Wavetable3D wt;
        wt.name = "Harmonic Growl";
        wt.frames.resize(16, std::vector<float>(TABLE_SIZE));
        for (int f = 0; f < 16; ++f) {
            int maxHarmonics = 1 + f;
            float peak = 0.01f;
            for (int i = 0; i < TABLE_SIZE; ++i) {
                float t = (float)i / TABLE_SIZE * 2.0f * PI;
                float s = 0.0f;
                for (int h = 1; h <= maxHarmonics; ++h)
                    s += std::sin(t * h) / h;
                wt.frames[f][i] = s;
                peak = std::max(peak, std::abs(s));
            }
            for (int i = 0; i < TABLE_SIZE; ++i)
                wt.frames[f][i] /= peak;
        }
        tables.push_back(wt);
    }
};
