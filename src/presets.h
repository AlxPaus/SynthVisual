#pragma once
#include <windows.h>
#include <commdlg.h>
#include <fstream>
#include <string>
#include "synth_globals.h"
#include "audio_engine.h"

inline std::string SaveFileDialog() { char filename[MAX_PATH] = ""; OPENFILENAMEA ofn; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = NULL; ofn.lpstrFilter = "Osci Preset (*.osci)\0*.osci\0All Files (*.*)\0*.*\0"; ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH; ofn.lpstrTitle = "Save Preset"; ofn.lpstrDefExt = "osci"; ofn.Flags = OFN_DONTADDTORECENT | OFN_OVERWRITEPROMPT; if (GetSaveFileNameA(&ofn)) return std::string(filename); return ""; }
inline std::string OpenFileDialog() { char filename[MAX_PATH] = ""; OPENFILENAMEA ofn; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = NULL; ofn.lpstrFilter = "Osci Preset (*.osci)\0*.osci\0All Files (*.*)\0*.*\0"; ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH; ofn.lpstrTitle = "Load Preset"; ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST; if (GetOpenFileNameA(&ofn)) return std::string(filename); return ""; }

inline void SavePreset(const std::string& filename) {
    std::ofstream out(filename); if (!out) return;
    out << "modMatrixSize=" << modMatrix.size() << "\n";
    for(size_t i=0; i<modMatrix.size(); ++i) { out << "mod_" << i << "=" << modMatrix[i].source << "," << modMatrix[i].target << "," << modMatrix[i].amount << "\n"; }
    out << "masterVolumeDb=" << masterVolumeDb << "\n"; out << "isMonoLegato=" << isMonoLegato << "\n"; out << "glideTime=" << glideTime << "\n";
    out << "subEnabled=" << subEnabled << "\n"; out << "subOctave=" << subOctave << "\n"; out << "subLevel=" << subLevel << "\n";
    out << "noiseEnabled=" << globalNoise.enabled << "\n"; out << "noiseLevel=" << globalNoise.level << "\n"; out << "noiseType=" << globalNoise.type << "\n";
    out << "oscAEnabled=" << oscAEnabled << "\n"; out << "currentTableIndexA=" << currentTableIndexA << "\n"; out << "oscA_Octave=" << oscA_Octave << "\n"; out << "pitchSemiA=" << pitchSemiA << "\n"; out << "wtPosA=" << wtPosA << "\n"; out << "unisonVoicesA=" << unisonVoicesA << "\n"; out << "unisonDetuneA=" << unisonDetuneA << "\n"; out << "unisonBlendA=" << unisonBlendA << "\n"; out << "oscALevel=" << oscALevel << "\n";
    out << "oscBEnabled=" << oscBEnabled << "\n"; out << "currentTableIndexB=" << currentTableIndexB << "\n"; out << "oscB_Octave=" << oscB_Octave << "\n"; out << "pitchSemiB=" << pitchSemiB << "\n"; out << "wtPosB=" << wtPosB << "\n"; out << "unisonVoicesB=" << unisonVoicesB << "\n"; out << "unisonDetuneB=" << unisonDetuneB << "\n"; out << "unisonBlendB=" << unisonBlendB << "\n"; out << "oscBLevel=" << oscBLevel << "\n";
    for(int i=0; i<3; ++i) { out << "envA_" << i << "=" << envA[i] << "\n"; out << "envD_" << i << "=" << envD[i] << "\n"; out << "envS_" << i << "=" << envS[i] << "\n"; out << "envR_" << i << "=" << envR[i] << "\n"; }
    out << "filterEnabled=" << filterEnabled << "\n"; out << "filterType=" << filterType << "\n"; out << "filterCutoff=" << filterCutoff << "\n"; out << "filterResonance=" << filterResonance << "\n";
    for(int i=0; i<2; ++i) { out << "lfoShape_" << i << "=" << globalLFOs[i].shape << "\n"; out << "lfoSyncMode_" << i << "=" << lfoSyncMode[i] << "\n"; out << "lfoRateHz_" << i << "=" << lfoRateHz[i] << "\n"; out << "lfoRateBPMIndex_" << i << "=" << lfoRateBPMIndex[i] << "\n"; }
    out << "globalBpm=" << globalBpm << "\n"; out << "distEnabled=" << globalDistortion.enabled << "\n"; out << "distDrive=" << globalDistortion.drive << "\n"; out << "distMix=" << globalDistortion.mix << "\n";
    out << "delEnabled=" << globalDelay.enabled << "\n"; out << "delTime=" << globalDelay.time << "\n"; out << "delFB=" << globalDelay.feedback << "\n"; out << "delMix=" << globalDelay.mix << "\n";
    out << "revEnabled=" << globalReverb.enabled << "\n"; out << "revSize=" << globalReverb.roomSize << "\n"; out << "revDamp=" << globalReverb.damping << "\n"; out << "revMix=" << globalReverb.mix << "\n";
}

inline void LoadPreset(const std::string& filename) {
    std::ifstream in(filename); if (!in) return; std::string line; std::lock_guard<std::mutex> lock(audioMutex); modMatrix.clear();
    try {
        while (std::getline(in, line)) {
            size_t pos = line.find('='); if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos); std::string val = line.substr(pos + 1); if (val.empty()) continue;
            if (key.rfind("mod_", 0) == 0 && key != "modMatrixSize") {
                size_t c1 = val.find(','); size_t c2 = val.rfind(',');
                if (c1 != std::string::npos && c2 != std::string::npos) modMatrix.push_back({std::stoi(val.substr(0, c1)), std::stoi(val.substr(c1+1, c2-c1-1)), std::stof(val.substr(c2+1))});
            }
            else if (key == "masterVolumeDb") masterVolumeDb = std::stof(val); else if (key == "isMonoLegato") isMonoLegato = std::stoi(val); else if (key == "glideTime") glideTime = std::stof(val);
            else if (key == "subEnabled") subEnabled = std::stoi(val); else if (key == "subOctave") subOctave = std::stoi(val); else if (key == "subLevel") subLevel = std::stof(val);
            else if (key == "noiseEnabled") globalNoise.enabled = std::stoi(val); else if (key == "noiseLevel") globalNoise.level = std::stof(val); else if (key == "noiseType") globalNoise.type = std::stoi(val);
            else if (key == "oscAEnabled") oscAEnabled = std::stoi(val); else if (key == "currentTableIndexA") currentTableIndexA = std::stoi(val); else if (key == "oscA_Octave") oscA_Octave = std::stoi(val); else if (key == "pitchSemiA") pitchSemiA = std::stoi(val); else if (key == "wtPosA") wtPosA = std::stof(val); else if (key == "unisonVoicesA") unisonVoicesA = std::stoi(val); else if (key == "unisonDetuneA") unisonDetuneA = std::stof(val); else if (key == "unisonBlendA") unisonBlendA = std::stof(val); else if (key == "oscALevel") oscALevel = std::stof(val);
            else if (key == "oscBEnabled") oscBEnabled = std::stoi(val); else if (key == "currentTableIndexB") currentTableIndexB = std::stoi(val); else if (key == "oscB_Octave") oscB_Octave = std::stoi(val); else if (key == "pitchSemiB") pitchSemiB = std::stoi(val); else if (key == "wtPosB") wtPosB = std::stof(val); else if (key == "unisonVoicesB") unisonVoicesB = std::stoi(val); else if (key == "unisonDetuneB") unisonDetuneB = std::stof(val); else if (key == "unisonBlendB") unisonBlendB = std::stof(val); else if (key == "oscBLevel") oscBLevel = std::stof(val);
            else if (key.rfind("envA_", 0) == 0) envA[std::stoi(key.substr(5))] = std::stof(val); else if (key.rfind("envD_", 0) == 0) envD[std::stoi(key.substr(5))] = std::stof(val); else if (key.rfind("envS_", 0) == 0) envS[std::stoi(key.substr(5))] = std::stof(val); else if (key.rfind("envR_", 0) == 0) envR[std::stoi(key.substr(5))] = std::stof(val);
            else if (key == "filterEnabled") filterEnabled = std::stoi(val); else if (key == "filterType") filterType = std::stoi(val); else if (key == "filterCutoff") filterCutoff = std::stof(val); else if (key == "filterResonance") filterResonance = std::stof(val);
            else if (key.rfind("lfoShape_", 0) == 0) globalLFOs[std::stoi(key.substr(9))].shape = std::stoi(val); else if (key.rfind("lfoSyncMode_", 0) == 0) lfoSyncMode[std::stoi(key.substr(12))] = std::stoi(val); else if (key.rfind("lfoRateHz_", 0) == 0) lfoRateHz[std::stoi(key.substr(10))] = std::stof(val); else if (key.rfind("lfoRateBPMIndex_", 0) == 0) lfoRateBPMIndex[std::stoi(key.substr(16))] = std::stoi(val); else if (key == "globalBpm") globalBpm = std::stof(val);
            else if (key == "distEnabled") globalDistortion.enabled = std::stoi(val); else if (key == "distDrive") globalDistortion.drive = std::stof(val); else if (key == "distMix") globalDistortion.mix = std::stof(val);
            else if (key == "delEnabled") globalDelay.enabled = std::stoi(val); else if (key == "delTime") globalDelay.time = std::stof(val); else if (key == "delFB") globalDelay.feedback = std::stof(val); else if (key == "delMix") globalDelay.mix = std::stof(val);
            else if (key == "revEnabled") globalReverb.enabled = std::stoi(val); else if (key == "revSize") globalReverb.roomSize = std::stof(val); else if (key == "revDamp") globalReverb.damping = std::stof(val); else if (key == "revMix") globalReverb.mix = std::stof(val);
        }
    } catch (...) { std::cerr << "Preset corrupted!" << std::endl; }
    UpdateOscillatorsTable(); UpdateEnvelopes(); UpdateFilters();
}