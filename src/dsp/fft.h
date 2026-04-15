#pragma once
#include <vector>
#include <complex>
#include <algorithm>
#include "constants.h"

// Iterative Cooley-Tukey FFT (in-place, complex input, power-of-2 size)
inline void computeFFT(std::vector<std::complex<float>>& data) {
    int n = (int)data.size();

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }

    // Butterfly passes
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * PI / len;
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + len / 2] * w;
                data[i + j]           = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
