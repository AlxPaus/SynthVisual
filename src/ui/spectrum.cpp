#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <mutex>
#include "synth/synth_globals.h"
#include "dsp/fft.h"
#include "ui/spectrum.h"

void UpdateSpectrum() {
    std::vector<std::complex<float>> fftData(FFT_SIZE);
    std::vector<float> ringSnapshot(FFT_SIZE, 0.0f);
    int ringPosSnapshot = 0;
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        ringSnapshot = g_synth.fftRingBuffer;
        ringPosSnapshot = g_synth.fftRingPos;
    }
    for (int i = 0; i < FFT_SIZE; ++i) {
        int   idx = (ringPosSnapshot + i) % FFT_SIZE;
        float win = 0.5f * (1.0f - std::cos(2.0f * PI * i / (FFT_SIZE - 1)));
        fftData[i] = std::complex<float>(ringSnapshot[idx] * win, 0.0f);
    }
    computeFFT(fftData);

    std::lock_guard<std::mutex> lock(audioMutex);
    for (int i = 0; i < FFT_SIZE / 2; ++i) {
        float mag = std::abs(fftData[i]) / (FFT_SIZE / 2.0f);
        float db  = 20.0f * std::log10(std::max(1e-6f, mag));
        float val = std::max(0.0f, std::min(1.0f, (db + 70.0f) / 70.0f));
        // Low-pass smoothing: 80% old, 20% new
        g_synth.spectrumSmooth[i] = g_synth.spectrumSmooth[i] * 0.8f + val * 0.2f;
    }
}
