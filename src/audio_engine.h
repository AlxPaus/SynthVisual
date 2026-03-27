#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <algorithm>
#include "synth_globals.h"

inline void UpdateOscillatorsTable() { for(auto& v : voicesA) v.setWavetable(&wtManager.tables[currentTableIndexA]); for(auto& v : voicesB) v.setWavetable(&wtManager.tables[currentTableIndexB]); }
inline void UpdateEnvelopes() { for(auto& v : voicesA) v.env.setParameters(envA[0], envD[0], envS[0], envR[0]); for(auto& v : voicesB) v.env.setParameters(envA[0], envD[0], envS[0], envR[0]); }
inline void UpdateFilters() { for(auto& v : voicesA) v.filter.enabled = filterEnabled; for(auto& v : voicesB) v.filter.enabled = filterEnabled; }

inline void TriggerPool(std::vector<WavetableOscillator>& pool, int baseNote, int osc_Octave, int semiOffset, int unisonCount, float detune, float blend, bool legatoBypassTrigger = false) {
    int voicesToTrigger = unisonCount;
    for (auto& v : pool) {
        if (!v.isActive() || legatoBypassTrigger) {
            if (!legatoBypassTrigger) v.noteOn(baseNote); 
            float spreadStr = (unisonCount > 1) ? 2.0f * (float)(unisonCount - voicesToTrigger) / (float)(unisonCount - 1) - 1.0f : 0.0f;
            float detuneSemitones = spreadStr * (detune * 0.5f);
            float freq = 440.0f * std::pow(2.0f, (baseNote + (osc_Octave * 12) + semiOffset - 69.0f + detuneSemitones) / 12.0f);
            v.setFrequency(freq); v.setPan(spreadStr * blend);
            if (!legatoBypassTrigger) { float basePhaseIndex = (globalPhase / 360.0f) * TABLE_SIZE; float randOffset = ((float)rand() / (float)RAND_MAX) * (globalPhaseRand / 100.0f) * TABLE_SIZE; v.setPhase(std::fmod(basePhaseIndex + randOffset, (float)TABLE_SIZE)); }
            voicesToTrigger--; if (voicesToTrigger <= 0) break; 
        }
    }
}

inline void NoteOn(int note) {
    std::lock_guard<std::mutex> lock(audioMutex); 
    if (note < 0 || note > 127 || noteState[note]) return;
    noteState[note] = true; 
    if (isMonoLegato) {
        heldNotesStack.push_back(note); targetGlideFreq = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); globalLastPolyFreq = targetGlideFreq;
        if (heldNotesStack.size() == 1) { 
            currentGlideFreq = targetGlideFreq; auxEnvs[0].trigger(); auxEnvs[1].trigger();
            if (oscAEnabled) TriggerPool(voicesA, note, oscA_Octave, pitchSemiA, unisonVoicesA, unisonDetuneA, unisonBlendA); if (oscBEnabled) TriggerPool(voicesB, note, oscB_Octave, pitchSemiB, unisonVoicesB, unisonDetuneB, unisonBlendB);
        } else { 
            if (oscAEnabled) TriggerPool(voicesA, note, oscA_Octave, pitchSemiA, unisonVoicesA, unisonDetuneA, unisonBlendA, true); if (oscBEnabled) TriggerPool(voicesB, note, oscB_Octave, pitchSemiB, unisonVoicesB, unisonDetuneB, unisonBlendB, true);
        }
    } else {
        globalLastPolyFreq = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); auxEnvs[0].trigger(); auxEnvs[1].trigger();
        if (oscAEnabled) TriggerPool(voicesA, note, oscA_Octave, pitchSemiA, unisonVoicesA, unisonDetuneA, unisonBlendA); if (oscBEnabled) TriggerPool(voicesB, note, oscB_Octave, pitchSemiB, unisonVoicesB, unisonDetuneB, unisonBlendB);
    }
}

inline void NoteOff(int note) {
    std::lock_guard<std::mutex> lock(audioMutex); if (note < 0 || note > 127) return; noteState[note] = false; 
    if (isMonoLegato) {
        auto it = std::find(heldNotesStack.begin(), heldNotesStack.end(), note); if (it != heldNotesStack.end()) heldNotesStack.erase(it);
        if (heldNotesStack.empty()) { for (auto& v : voicesA) v.noteOff(); for (auto& v : voicesB) v.noteOff(); auxEnvs[0].release(); auxEnvs[1].release(); } 
        else { int prevNote = heldNotesStack.back(); targetGlideFreq = 440.0f * std::pow(2.0f, (prevNote - 69.0f) / 12.0f); globalLastPolyFreq = targetGlideFreq; }
    } else {
        for (auto& v : voicesA) if (v.getNote() == note) v.noteOff(); for (auto& v : voicesB) if (v.getNote() == note) v.noteOff();
        bool anyActive = false; for(auto& v : voicesA) if(v.isActive()) anyActive=true; if(!anyActive){auxEnvs[0].release(); auxEnvs[1].release();}
    }
}

inline HMIDIIN hMidiIn = nullptr; inline std::vector<std::string> midiPorts; inline int selectedMidiPort = -1;
inline void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) { if (wMsg == MIM_DATA) { unsigned char status = dwParam1 & 0xFF; unsigned char note = (dwParam1 >> 8) & 0xFF; unsigned char velocity = (dwParam1 >> 16) & 0xFF; unsigned char type = status & 0xF0; if (type == 0x90 && velocity > 0) NoteOn(note); else if (type == 0x80 || (type == 0x90 && velocity == 0)) NoteOff(note); } }
inline void RefreshMidiPorts() { midiPorts.clear(); UINT numDevs = midiInGetNumDevs(); for (UINT i = 0; i < numDevs; i++) { MIDIINCAPSA caps; if (midiInGetDevCapsA(i, &caps, sizeof(MIDIINCAPSA)) == MMSYSERR_NOERROR) { midiPorts.push_back(caps.szPname); } } }
inline void OpenMidiPort(int portIndex) { if (hMidiIn) { midiInStop(hMidiIn); midiInClose(hMidiIn); hMidiIn = nullptr; } if (portIndex >= 0 && portIndex < (int)midiPorts.size()) { if (midiInOpen(&hMidiIn, portIndex, (DWORD_PTR)MidiInProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) { midiInStart(hMidiIn); } } }

inline float GetModSum(int target_id) {
    float sum = 0.0f;
    for (auto& m : modMatrix) {
        if (m.target == target_id) {
            float srcVal = 0.0f;
            if (m.source == SRC_LFO1) srcVal = globalLFOs[0].currentValue;
            if (m.source == SRC_LFO2) srcVal = globalLFOs[1].currentValue;
            if (m.source == SRC_ENV1) { float maxEnv = 0.0f; for (int v = 0; v < MAX_VOICES; ++v) if (voicesA[v].isActive()) maxEnv = std::max(maxEnv, voicesA[v].env.currentLevel); srcVal = maxEnv; }
            if (m.source == SRC_ENV2) srcVal = auxEnvs[0].val;
            if (m.source == SRC_ENV3) srcVal = auxEnvs[1].val;
            sum += srcVal * m.amount;
        }
    }
    return sum;
}
inline float GetModAmountForUI(int target_id) { for (auto& m : modMatrix) if (m.source == uiSelectedModSource && m.target == target_id) return m.amount; return 0.0f; }

inline void data_callback(float* pOut, int frameCount) {
    std::lock_guard<std::mutex> lock(audioMutex); 
    for (int i = 0; i < frameCount * 2; ++i) pOut[i] = 0.0f;
    
    for (int i=0; i<2; ++i) {
        if (lfoSyncMode[i] == 1) { float beatHz = globalBpm / 60.0f; float mults[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f }; globalLFOs[i].rateHz = beatHz * mults[lfoRateBPMIndex[i]]; } 
        else { globalLFOs[i].rateHz = lfoRateHz[i]; }
        globalLFOs[i].advance(frameCount);
    }

    float mod_FiltCut = GetModSum(TGT_FILT_CUTOFF); float mod_FiltRes = GetModSum(TGT_FILT_RES);
    float mod_WTA = GetModSum(TGT_OSCA_WTPOS); float mod_LevA = GetModSum(TGT_OSCA_LEVEL); float mod_DetA = GetModSum(TGT_OSCA_DETUNE); float mod_BndA = GetModSum(TGT_OSCA_BLEND); float mod_PitA = GetModSum(TGT_OSCA_PITCH);
    float mod_WTB = GetModSum(TGT_OSCB_WTPOS); float mod_LevB = GetModSum(TGT_OSCB_LEVEL); float mod_DetB = GetModSum(TGT_OSCB_DETUNE); float mod_BndB = GetModSum(TGT_OSCB_BLEND); float mod_PitB = GetModSum(TGT_OSCB_PITCH);

    float currentModCutoff = std::max(20.0f, std::min(20000.0f, filterCutoff * std::pow(2.0f, mod_FiltCut * 5.0f)));
    float currentModRes = std::max(0.1f, std::min(10.0f, filterResonance + mod_FiltRes * 5.0f));
    float currentWTPosA = std::max(0.0f, std::min(1.0f, wtPosA + mod_WTA)); float currentLevelA = std::max(0.0f, std::min(1.0f, oscALevel + mod_LevA)); float currentDetA = std::max(0.0f, std::min(1.0f, unisonDetuneA + mod_DetA)); float currentBndA = std::max(0.0f, std::min(1.0f, unisonBlendA + mod_BndA));
    float currentWTPosB = std::max(0.0f, std::min(1.0f, wtPosB + mod_WTB)); float currentLevelB = std::max(0.0f, std::min(1.0f, oscBLevel + mod_LevB)); float currentDetB = std::max(0.0f, std::min(1.0f, unisonDetuneB + mod_DetB)); float currentBndB = std::max(0.0f, std::min(1.0f, unisonBlendB + mod_BndB));

    voicesA[0].setWTPos(currentWTPosA); voicesA[0].filter.setCoefficients((FilterType)filterType, currentModCutoff, currentModRes, 44100.0f);
    voicesB[0].setWTPos(currentWTPosB); voicesB[0].filter.setCoefficients((FilterType)filterType, currentModCutoff, currentModRes, 44100.0f);
    for(size_t j = 1; j < voicesA.size(); ++j) { if (voicesA[j].isActive()) { voicesA[j].setWTPos(currentWTPosA); voicesA[j].filter.setCoefficients((FilterType)filterType, currentModCutoff, currentModRes, 44100.0f); } }
    for(size_t j = 1; j < voicesB.size(); ++j) { if (voicesB[j].isActive()) { voicesB[j].setWTPos(currentWTPosB); voicesB[j].filter.setCoefficients((FilterType)filterType, currentModCutoff, currentModRes, 44100.0f); } }

    float gcA = 1.0f / std::sqrt((float)unisonVoicesA); float gcB = 1.0f / std::sqrt((float)unisonVoicesB); float mGain = std::pow(10.0f, masterVolumeDb / 20.0f); 
    float glideAlpha = 1.0f; if (glideTime > 0.001f) glideAlpha = 1.0f - std::exp(-1.0f / (44100.0f * (glideTime / 3.0f)));

    for (int i = 0; i < frameCount; ++i) {
        auxEnvs[0].process(envA[1], envD[1], envS[1], envR[1], 44100.0f);
        auxEnvs[1].process(envA[2], envD[2], envS[2], envR[2], 44100.0f);

        if (isMonoLegato && !heldNotesStack.empty()) {
            currentGlideFreq += glideAlpha * (targetGlideFreq - currentGlideFreq); globalLastPolyFreq = currentGlideFreq; 
            if (oscAEnabled) { for (int u = 0; u < unisonVoicesA; ++u) { if (voicesA[u].isActive()) { float spreadStr = (unisonVoicesA > 1) ? 2.0f * (float)(unisonVoicesA - 1 - u) / (float)(unisonVoicesA - 1) - 1.0f : 0.0f; float pitchMult = std::pow(2.0f, (oscA_Octave * 12 + pitchSemiA + mod_PitA*24.0f + (spreadStr * currentDetA * 0.5f)) / 12.0f); voicesA[u].setFrequency(currentGlideFreq * pitchMult); voicesA[u].setPan(spreadStr * currentBndA); } } }
            if (oscBEnabled) { for (int u = 0; u < unisonVoicesB; ++u) { if (voicesB[u].isActive()) { float spreadStr = (unisonVoicesB > 1) ? 2.0f * (float)(unisonVoicesB - 1 - u) / (float)(unisonVoicesB - 1) - 1.0f : 0.0f; float pitchMult = std::pow(2.0f, (oscB_Octave * 12 + pitchSemiB + mod_PitB*24.0f + (spreadStr * currentDetB * 0.5f)) / 12.0f); voicesB[u].setFrequency(currentGlideFreq * pitchMult); voicesB[u].setPan(spreadStr * currentBndB); } } }
        } else {
            if (oscAEnabled) { for (size_t u = 0; u < voicesA.size(); ++u) { if (voicesA[u].isActive()) { float freq = 440.0f * std::pow(2.0f, (voicesA[u].getNote() + oscA_Octave * 12 + pitchSemiA + mod_PitA*24.0f - 69.0f) / 12.0f); voicesA[u].setFrequency(freq); } } }
            if (oscBEnabled) { for (size_t u = 0; u < voicesB.size(); ++u) { if (voicesB[u].isActive()) { float freq = 440.0f * std::pow(2.0f, (voicesB[u].getNote() + oscB_Octave * 12 + pitchSemiB + mod_PitB*24.0f - 69.0f) / 12.0f); voicesB[u].setFrequency(freq); } } }
        }

        float mixL = 0.0f, mixR = 0.0f; float maxEnv = 0.0f; 
        if (oscAEnabled) { for (auto& v : voicesA) { if (v.isActive()) { float p = v.getPan(); float s = v.getSample() * currentLevelA; mixL += s * (1.0f-p)*0.5f*gcA*0.15f; mixR += s * (1.0f+p)*0.5f*gcA*0.15f; maxEnv = std::max(maxEnv, v.env.currentLevel); } } }
        if (oscBEnabled) { for (auto& v : voicesB) { if (v.isActive()) { float p = v.getPan(); float s = v.getSample() * currentLevelB; mixL += s * (1.0f-p)*0.5f*gcB*0.15f; mixR += s * (1.0f+p)*0.5f*gcB*0.15f; maxEnv = std::max(maxEnv, v.env.currentLevel); } } }

        float mod_SubLev = GetModSum(TGT_SUB_LEVEL); float mod_NoiseLev = GetModSum(TGT_NOISE_LEVEL);
        if (subEnabled && maxEnv > 0.001f) { float subF = globalLastPolyFreq * std::pow(2.0f, (float)subOctave); subPhase += subF / 44100.0f; if (subPhase >= 1.0f) subPhase -= 1.0f; float subS = std::sin(subPhase * 2.0f * 3.14159265f) * std::max(0.0f, std::min(1.0f, subLevel + mod_SubLev)) * maxEnv * 0.2f; mixL += subS; mixR += subS; }
        if (globalNoise.enabled && maxEnv > 0.001f) { float nS = globalNoise.process() * std::max(0.0f, std::min(1.0f, globalNoise.level + mod_NoiseLev)) * maxEnv * 0.15f; mixL += nS; mixR += nS; }

        globalDistortion.drive = std::max(0.0f, std::min(1.0f, globalDistortion.drive + GetModSum(TGT_DIST_DRIVE))); globalDistortion.mix = std::max(0.0f, std::min(1.0f, globalDistortion.mix + GetModSum(TGT_DIST_MIX)));
        globalDelay.time = std::max(0.01f, std::min(1.5f, globalDelay.time + GetModSum(TGT_DEL_TIME))); globalDelay.feedback = std::max(0.0f, std::min(0.95f, globalDelay.feedback + GetModSum(TGT_DEL_FB))); globalDelay.mix = std::max(0.0f, std::min(1.0f, globalDelay.mix + GetModSum(TGT_DEL_MIX)));
        globalReverb.roomSize = std::max(0.0f, std::min(0.98f, globalReverb.roomSize + GetModSum(TGT_REV_SIZE))); globalReverb.damping = std::max(0.0f, std::min(1.0f, globalReverb.damping + GetModSum(TGT_REV_DAMP))); globalReverb.mix = std::max(0.0f, std::min(1.0f, globalReverb.mix + GetModSum(TGT_REV_MIX)));

        mixL = globalDistortion.process(mixL); mixR = globalDistortion.process(mixR);
        globalDelay.process(mixL, mixR, 44100.0f); globalReverb.process(mixL, mixR); 
        
        float finalL = mixL * mGain; float finalR = mixR * mGain;
        pOut[2*i] = finalL; pOut[2*i+1] = finalR;

        float monoSample = (finalL + finalR) * 0.5f;
        if (!scopeTriggered && lastScopeSample <= 0.0f && monoSample > 0.0f) { scopeTriggered = true; scopeIndex = 0; }
        if (scopeTriggered && scopeIndex < SCOPE_SIZE) scopeBuffer[scopeIndex++] = monoSample;
        if (scopeIndex >= SCOPE_SIZE) scopeTriggered = false; 
        lastScopeSample = monoSample; fftRingBuffer[fftRingPos] = monoSample; fftRingPos = (fftRingPos + 1) % FFT_SIZE;
    }
}