#pragma once

#ifndef PI
#define PI 3.14159265358979323846f
#endif

constexpr float kSampleRate      = 44100.0f;
constexpr int   TABLE_SIZE       = 2048;
constexpr int   kMaxDelaySamples = 88200;  // 2 seconds of stereo delay at 44100 Hz
constexpr int   MAX_VOICES       = 128;
constexpr int   SCOPE_SIZE       = 512;
constexpr int   FFT_SIZE         = 2048;
