#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// === ADSR ENVELOPE ===
enum EnvState { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
class ADSR {
public:
    float attackTime, decayTime, sustainLevel, releaseTime, sampleRate, currentLevel, releaseStep;
    EnvState state;
    ADSR(float sr = 44100.0f) : sampleRate(sr), state(ENV_IDLE), currentLevel(0.0f), releaseStep(0.0f) { setParameters(0.05f, 0.5f, 0.7f, 0.3f); }
    void setParameters(float a, float d, float s, float r) {
        attackTime = std::max(0.001f, a); decayTime = std::max(0.001f, d);
        sustainLevel = s; releaseTime = std::max(0.001f, r);
    }
    void noteOn() { state = ENV_ATTACK; }
    void noteOff() { state = ENV_RELEASE; releaseStep = currentLevel / (releaseTime * sampleRate); }
    float process() {
        switch (state) {
            case ENV_IDLE: currentLevel = 0.0f; break;
            case ENV_ATTACK: currentLevel += 1.0f / (attackTime * sampleRate); if (currentLevel >= 1.0f) { currentLevel = 1.0f; state = ENV_DECAY; } break;
            case ENV_DECAY: currentLevel -= (1.0f - sustainLevel) / (decayTime * sampleRate); if (currentLevel <= sustainLevel) { currentLevel = sustainLevel; state = ENV_SUSTAIN; } break;
            case ENV_SUSTAIN: currentLevel = sustainLevel; break;
            case ENV_RELEASE: currentLevel -= releaseStep; if (currentLevel <= 0.0f) { currentLevel = 0.0f; state = ENV_IDLE; } break;
        }
        return currentLevel;
    }
    bool isActive() const { return state != ENV_IDLE; }
};

// === BIQUAD FILTER ===
// === BIQUAD FILTER ===
// === BIQUAD FILTER ===
enum FilterType { FILT_LOWPASS, FILT_HIGHPASS, FILT_BANDPASS };
class BiquadFilter {
public:
    bool enabled;
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;

    // Инициализируем нулями только при создании, а reset() очищает только историю звука
    BiquadFilter() : enabled(false), b0(0), b1(0), b2(0), a1(0), a2(0) { reset(); }

    void reset() { 
        x1 = x2 = y1 = y2 = 0.0f; 
        // a и b больше не обнуляем!
    }

    // ... остальной код BiquadFilter (setCoefficients, getMagnitude, process) остается без изменений

    void setCoefficients(FilterType type, float cutoffHz, float q, float sampleRate) {
        if (!enabled) return;
        float cutoff = std::min(cutoffHz, sampleRate * 0.49f);
        float w0 = 2.0f * PI * cutoff / sampleRate;
        float cosW0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * std::max(0.1f, q));
        float a0_inv = 1.0f;

        switch (type) {
            case FILT_LOWPASS:
                b0 = (1.0f - cosW0) / 2.0f; b1 = 1.0f - cosW0; b2 = (1.0f - cosW0) / 2.0f;
                a0_inv = 1.0f / (1.0f + alpha); a1 = -2.0f * cosW0; a2 = 1.0f - alpha; break;
            case FILT_HIGHPASS:
                b0 = (1.0f + cosW0) / 2.0f; b1 = -(1.0f + cosW0); b2 = (1.0f + cosW0) / 2.0f;
                a0_inv = 1.0f / (1.0f + alpha); a1 = -2.0f * cosW0; a2 = 1.0f - alpha; break;
            case FILT_BANDPASS:
                b0 = alpha; b1 = 0.0f; b2 = -alpha;
                a0_inv = 1.0f / (1.0f + alpha); a1 = -2.0f * cosW0; a2 = 1.0f - alpha; break;
        }
        b0 *= a0_inv; b1 *= a0_inv; b2 *= a0_inv; a1 *= a0_inv; a2 *= a0_inv;
    }

    // НОВАЯ ФУНКЦИЯ: Вычисляет амплитуду (громкость) для графика
    float getMagnitude(float freq, float sampleRate) const {
        if (!enabled) return 1.0f;
        float w = 2.0f * PI * freq / sampleRate;
        float cosw = std::cos(w); float cos2w = std::cos(2.0f * w);
        float sinw = std::sin(w); float sin2w = std::sin(2.0f * w);

        // Числитель и знаменатель передаточной функции (Transfer Function)
        float numRe = b0 + b1 * cosw + b2 * cos2w;
        float numIm = -(b1 * sinw + b2 * sin2w);
        float denRe = 1.0f + a1 * cosw + a2 * cos2w;
        float denIm = -(a1 * sinw + a2 * sin2w);

        float numMagSq = numRe * numRe + numIm * numIm;
        float denMagSq = denRe * denRe + denIm * denIm;

        if (denMagSq == 0.0f) return 0.0f;
        return std::sqrt(numMagSq / denMagSq);
    }

    float process(float input) {
        if (!enabled) return input;
        float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = input; y2 = y1; y1 = output;
        if (std::abs(y1) < 1.0e-20f) y1 = 0.0f;
        return output;
    }
};

// === WAVETABLES ===
const int TABLE_SIZE = 2048;
struct Wavetable3D { std::string name; std::vector<std::vector<float>> frames; };
class WavetableManager {
public:
    std::vector<Wavetable3D> tables;
    WavetableManager() { generateBasicShapes(); generatePWMSweep(); generateFMWub(); generateHarmonicGrowl(); }
private:
    void generateBasicShapes() {
        Wavetable3D wt; wt.name = "Basic Shapes"; wt.frames.resize(4, std::vector<float>(TABLE_SIZE));
        for (int i = 0; i < TABLE_SIZE; ++i) { float t = (float)i / TABLE_SIZE; wt.frames[0][i] = std::sin(t * 2.0f * PI); wt.frames[1][i] = 2.0f * std::abs(2.0f * t - 1.0f) - 1.0f; wt.frames[2][i] = 1.0f - 2.0f * t; wt.frames[3][i] = (i < TABLE_SIZE / 2) ? 1.0f : -1.0f; }
        tables.push_back(wt);
    }
    void generatePWMSweep() {
        Wavetable3D wt; wt.name = "PWM Sweep"; wt.frames.resize(16, std::vector<float>(TABLE_SIZE));
        for (int f = 0; f < 16; ++f) { float pw = 0.5f - (0.45f * ((float)f / 15.0f)); for (int i = 0; i < TABLE_SIZE; ++i) wt.frames[f][i] = ((float)i / TABLE_SIZE < pw) ? 1.0f : -1.0f; }
        tables.push_back(wt);
    }
    void generateFMWub() {
        Wavetable3D wt; wt.name = "FM Wub"; wt.frames.resize(16, std::vector<float>(TABLE_SIZE));
        for (int f = 0; f < 16; ++f) { float mod = 5.0f * ((float)f / 15.0f); for (int i = 0; i < TABLE_SIZE; ++i) { float t = (float)i / TABLE_SIZE * 2.0f * PI; wt.frames[f][i] = std::sin(t + mod * std::sin(t)); }}
        tables.push_back(wt);
    }
    void generateHarmonicGrowl() {
        Wavetable3D wt; wt.name = "Harmonic Growl"; wt.frames.resize(16, std::vector<float>(TABLE_SIZE));
        for (int f = 0; f < 16; ++f) { int maxH = 1 + f; float maxV = 0.01f; for (int i = 0; i < TABLE_SIZE; ++i) { float t = (float)i / TABLE_SIZE * 2.0f * PI; float s = 0; for(int h=1; h<=maxH; h++) s += std::sin(t*h)/h; wt.frames[f][i] = s; maxV=std::max(maxV,std::abs(s)); } for(int i=0; i<TABLE_SIZE; i++) wt.frames[f][i]/=maxV; }
        tables.push_back(wt);
    }
};

// === LFO (LOW FREQUENCY OSCILLATOR) ===
class LFO {
public:
    float phase = 0.0f;
    float rateHz = 1.0f;
    float sampleRate = 44100.0f;
    float currentValue = 0.0f;

    // Продвигаем LFO вперед на количество аудио-фреймов
    void advance(int frames) {
        phase += (rateHz / sampleRate) * frames;
        while (phase >= 1.0f) phase -= 1.0f;
        
        // Генерируем двуполярную треугольную волну (от -1.0 до 1.0)
        if (phase < 0.25f) currentValue = phase * 4.0f;
        else if (phase < 0.75f) currentValue = 1.0f - (phase - 0.25f) * 4.0f;
        else currentValue = -1.0f + (phase - 0.75f) * 4.0f;
    }
};

// === OSCILLATOR ===
class WavetableOscillator {
public:
    ADSR env;
    BiquadFilter filter; // <--- Встраиваем фильтр

    WavetableOscillator(float sampleRate) 
        : sampleRate(sampleRate), phase(0.0f), increment(0.0f), frequency(440.0f), 
          currentMidiNote(-1), pan(0.0f), wtPosNormalized(0.0f), currentTable(nullptr), env(sampleRate)
    {
        interpolatedWave.resize(TABLE_SIZE);
    }
    const std::vector<float>& getWavetableData() const { return interpolatedWave; }
    void setWavetable(const Wavetable3D* table) { currentTable = table; updateInterpolatedWave(); }
    void setWTPos(float pos) { wtPosNormalized = pos; updateInterpolatedWave(); }
    void updateInterpolatedWave() {
        if (!currentTable || currentTable->frames.empty()) return;
        int numFrames = (int)currentTable->frames.size();
        float preciseFrame = wtPosNormalized * (numFrames - 1);
        int frame1 = std::min((int)std::floor(preciseFrame), numFrames - 1);
        int frame2 = std::min((int)std::ceil(preciseFrame), numFrames - 1);
        float mix = preciseFrame - frame1;
        for (int i = 0; i < TABLE_SIZE; ++i) interpolatedWave[i] = currentTable->frames[frame1][i] * (1.0f - mix) + currentTable->frames[frame2][i] * mix;
    }
    void setPan(float p) { pan = p; } float getPan() const { return pan; }
    void setPhase(float p) { phase = p; }
    void setFrequency(float freq) { frequency = freq; increment = (TABLE_SIZE * frequency) / sampleRate; }
    void noteOn(int midiNote) {
        currentMidiNote = midiNote;
        float freq = 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
        setFrequency(freq);
        env.noteOn();
        filter.reset(); // <--- Сброс фильтра при новой ноте
    }
    void noteOff() { env.noteOff(); }
    bool isActive() const { return env.isActive(); } 
    int getNote() const { return currentMidiNote; }

    float getSample() {
        if (!isActive() || !currentTable) return 0.0f;
        int index0 = (int)phase; int index1 = (index0 + 1) % TABLE_SIZE;
        float frac = phase - (float)index0;
        float rawSample = interpolatedWave[index0] + frac * (interpolatedWave[index1] - interpolatedWave[index0]);
        phase += increment; while (phase >= TABLE_SIZE) phase -= TABLE_SIZE;
        
        // 1. Применяем огибающую
        float envSample = rawSample * env.process();
        // 2. Пропускаем через фильтр
        return filter.process(envSample);
    }

private:
    std::vector<float> interpolatedWave;
    const Wavetable3D* currentTable;
    float sampleRate; float phase; float increment; float frequency;
    int currentMidiNote; float pan; float wtPosNormalized;
};