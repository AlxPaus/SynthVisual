#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <iostream>
#include "synth/synth_globals.h"
#include "synth/audio_engine.h"
#include "synth/presets.h"

std::string SaveFileDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn       = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFilter  = "Osci Preset (*.osci)\0*.osci\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = "Save Preset";
    ofn.lpstrDefExt  = "osci";
    ofn.Flags        = OFN_DONTADDTORECENT | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameA(&ofn) ? std::string(filename) : "";
}

std::string OpenFileDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn       = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Osci Preset (*.osci)\0*.osci\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = filename;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Load Preset";
    ofn.Flags       = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;
    return GetOpenFileNameA(&ofn) ? std::string(filename) : "";
}

// Each entry maps a key string to save/load lambdas bound to g_synth fields.
// Lambda references are stable because g_synth has global lifetime.

struct ParamEntry {
    std::string key;
    std::function<void(std::ofstream&)>     save;
    std::function<void(const std::string&)> load;
};

static std::vector<ParamEntry> buildParamTable() {
    std::vector<ParamEntry> t;

    auto addF = [&](std::string key, float& ref) {
        t.push_back({ key,
            [key, &ref](std::ofstream& out) { out << key << "=" << ref << "\n"; },
            [&ref](const std::string& v) { ref = std::stof(v); }
        });
    };
    auto addI = [&](std::string key, int& ref) {
        t.push_back({ key,
            [key, &ref](std::ofstream& out) { out << key << "=" << ref << "\n"; },
            [&ref](const std::string& v) { ref = std::stoi(v); }
        });
    };
    auto addB = [&](std::string key, bool& ref) {
        t.push_back({ key,
            [key, &ref](std::ofstream& out) { out << key << "=" << (int)ref << "\n"; },
            [&ref](const std::string& v) { ref = (bool)std::stoi(v); }
        });
    };

    addF("masterVolumeDb", g_synth.masterVolumeDb);
    addB("isMonoLegato",   g_synth.monoLegato);
    addF("glideTime",      g_synth.glideTime);

    addB("subEnabled", g_synth.subEnabled);
    addI("subOctave",  g_synth.subOctave);
    addF("subLevel",   g_synth.subLevel);

    addB("noiseEnabled", g_synth.noise.enabled);
    addF("noiseLevel",   g_synth.noise.level);
    addI("noiseType",    g_synth.noise.type);

    // Oscillator keys match the legacy preset format for backward compatibility
    addB("oscAEnabled",        g_synth.oscA.enabled);
    addI("currentTableIndexA", g_synth.oscA.tableIndex);
    addI("oscA_Octave",        g_synth.oscA.octave);
    addI("pitchSemiA",         g_synth.oscA.semi);
    addF("wtPosA",             g_synth.oscA.wtPos);
    addI("unisonVoicesA",      g_synth.oscA.unisonVoices);
    addF("unisonDetuneA",      g_synth.oscA.unisonDetune);
    addF("unisonBlendA",       g_synth.oscA.unisonBlend);
    addF("oscALevel",          g_synth.oscA.level);

    addB("oscBEnabled",        g_synth.oscB.enabled);
    addI("currentTableIndexB", g_synth.oscB.tableIndex);
    addI("oscB_Octave",        g_synth.oscB.octave);
    addI("pitchSemiB",         g_synth.oscB.semi);
    addF("wtPosB",             g_synth.oscB.wtPos);
    addI("unisonVoicesB",      g_synth.oscB.unisonVoices);
    addF("unisonDetuneB",      g_synth.oscB.unisonDetune);
    addF("unisonBlendB",       g_synth.oscB.unisonBlend);
    addF("oscBLevel",          g_synth.oscB.level);

    for (int i = 0; i < 3; ++i) {
        std::string idx = std::to_string(i);
        addF("envA_" + idx, g_synth.envParams[i].attack);
        addF("envD_" + idx, g_synth.envParams[i].decay);
        addF("envS_" + idx, g_synth.envParams[i].sustain);
        addF("envR_" + idx, g_synth.envParams[i].release);
    }

    addB("filterEnabled",   g_synth.filter.enabled);
    addI("filterType",      g_synth.filter.type);
    addF("filterCutoff",    g_synth.filter.cutoff);
    addF("filterResonance", g_synth.filter.resonance);

    for (int i = 0; i < 2; ++i) {
        std::string idx = std::to_string(i);
        addI("lfoShape_"        + idx, g_synth.lfos[i].shape);
        addI("lfoSyncMode_"     + idx, g_synth.lfoConfig[i].syncMode);
        addF("lfoRateHz_"       + idx, g_synth.lfoConfig[i].rateHz);
        addI("lfoRateBPMIndex_" + idx, g_synth.lfoConfig[i].bpmRateIndex);
    }
    addF("globalBpm", g_synth.bpm);

    addB("distEnabled", g_synth.distortion.enabled);
    addF("distDrive",   g_synth.distortion.drive);
    addF("distMix",     g_synth.distortion.mix);

    addB("delEnabled", g_synth.delay.enabled);
    addF("delTime",    g_synth.delay.time);
    addF("delFB",      g_synth.delay.feedback);
    addF("delMix",     g_synth.delay.mix);

    addB("revEnabled", g_synth.reverb.enabled);
    addF("revSize",    g_synth.reverb.roomSize);
    addF("revDamp",    g_synth.reverb.damping);
    addF("revMix",     g_synth.reverb.mix);

    return t;
}

void SavePreset(const std::string& filename) {
    std::ofstream out(filename);
    if (!out) return;

    out << "modMatrixSize=" << g_synth.modMatrix.size() << "\n";
    for (size_t i = 0; i < g_synth.modMatrix.size(); ++i) {
        const auto& m = g_synth.modMatrix[i];
        out << "mod_" << i << "=" << m.source << "," << m.target << "," << m.amount << "\n";
    }

    for (const auto& entry : buildParamTable())
        entry.save(out);
}

void LoadPreset(const std::string& filename) {
    std::ifstream in(filename);
    if (!in) return;

    std::lock_guard<std::mutex> lock(audioMutex);
    g_synth.modMatrix.clear();

    auto paramTable = buildParamTable();
    std::unordered_map<std::string, std::function<void(const std::string&)>> loadMap;
    for (const auto& entry : paramTable)
        loadMap[entry.key] = entry.load;

    std::string line;
    try {
        while (std::getline(in, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (val.empty()) continue;

            if (key.rfind("mod_", 0) == 0 && key != "modMatrixSize") {
                size_t c1 = val.find(',');
                size_t c2 = val.rfind(',');
                if (c1 == std::string::npos || c1 == c2) continue;
                int   src = std::stoi(val.substr(0, c1));
                int   tgt = std::stoi(val.substr(c1 + 1, c2 - c1 - 1));
                float amt = std::stof(val.substr(c2 + 1));
                g_synth.modMatrix.push_back({ src, tgt, amt });
                continue;
            }

            auto it = loadMap.find(key);
            if (it != loadMap.end())
                it->second(val);
        }
    } catch (const std::exception& e) {
        std::cerr << "Preset load error: " << e.what() << "\n";
    }

    UpdateOscillatorsTable();
    UpdateEnvelopes();
    UpdateFilters();
}
