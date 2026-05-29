#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <algorithm>

inline bool WriteWav(const std::string& path, const std::vector<float>& interleaved,
                     int sampleRate, int channels)
{
    if (interleaved.empty()) return false;

    int numSamples  = (int)interleaved.size();
    int byteRate    = sampleRate * channels * 2;
    int blockAlign  = channels * 2;
    int dataSize    = numSamples * 2;
    int chunkSize   = 36 + dataSize;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    auto write32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    auto write16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };

    f.write("RIFF", 4);
    write32(chunkSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    write32(16);
    write16(1);                        // PCM
    write16((uint16_t)channels);
    write32((uint32_t)sampleRate);
    write32((uint32_t)byteRate);
    write16((uint16_t)blockAlign);
    write16(16);                       // bits per sample
    f.write("data", 4);
    write32((uint32_t)dataSize);

    for (float s : interleaved) {
        float clamped  = std::max(-1.0f, std::min(1.0f, s));
        int16_t sample = (int16_t)(clamped * 32767.0f);
        f.write(reinterpret_cast<char*>(&sample), 2);
    }

    return f.good();
}
