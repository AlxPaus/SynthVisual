#pragma once
#include <mutex>
#include "synth/synth_state.h"

inline SynthState g_synth;
inline std::mutex audioMutex;
