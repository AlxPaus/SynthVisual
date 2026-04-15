#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <algorithm>
#include "synth_globals.h"
#include "audio_engine.h"

HMIDIIN              hMidiIn        = nullptr;
std::vector<std::string> midiPorts  = {};
int                  selectedMidiPort = -1;

// === VOICE MANAGEMENT ===

void UpdateOscillatorsTable() {
    for (auto& v : g_synth.voicesA)
        v.setWavetable(&g_synth.wavetables.tables[g_synth.oscA.tableIndex]);
    for (auto& v : g_synth.voicesB)
        v.setWavetable(&g_synth.wavetables.tables[g_synth.oscB.tableIndex]);
}

void UpdateEnvelopes() {
    const EnvParams& p = g_synth.envParams[0];
    for (auto& v : g_synth.voicesA) v.env.setParameters(p.attack, p.decay, p.sustain, p.release);
    for (auto& v : g_synth.voicesB) v.env.setParameters(p.attack, p.decay, p.sustain, p.release);
}

void UpdateFilters() {
    for (auto& v : g_synth.voicesA) v.filter.enabled = g_synth.filter.enabled;
    for (auto& v : g_synth.voicesB) v.filter.enabled = g_synth.filter.enabled;
}

// === NOTE ALLOCATION ===

static void TriggerVoicePool(
    std::vector<WavetableOscillator>& pool,
    const OscConfig& cfg,
    int baseNote,
    bool legatoSlide = false)
{
    int remaining = cfg.unisonVoices;
    for (auto& v : pool) {
        if (!v.isActive() || legatoSlide) {
            if (!legatoSlide) v.noteOn(baseNote);

            float spread = (cfg.unisonVoices > 1)
                ? 2.0f * (float)(cfg.unisonVoices - remaining) / (float)(cfg.unisonVoices - 1) - 1.0f
                : 0.0f;
            float detuneSemitones = spread * (cfg.unisonDetune * 0.5f);
            float freq = 440.0f * std::pow(2.0f,
                (baseNote + cfg.octave * 12 + cfg.semi - 69.0f + detuneSemitones) / 12.0f);

            v.setFrequency(freq);
            v.setPan(spread * cfg.unisonBlend);

            if (!legatoSlide) {
                float base   = (g_synth.startPhase / 360.0f) * TABLE_SIZE;
                float offset = ((float)rand() / (float)RAND_MAX)
                             * (g_synth.startPhaseRand / 100.0f) * TABLE_SIZE;
                v.setPhase(std::fmod(base + offset, (float)TABLE_SIZE));
            }
            if (--remaining <= 0) break;
        }
    }
}

void NoteOn(int note) {
    std::lock_guard<std::mutex> lock(audioMutex);
    if (note < 0 || note > 127 || g_synth.noteActive[note]) return;
    g_synth.noteActive[note] = true;

    if (g_synth.monoLegato) {
        g_synth.heldNotes.push_back(note);
        g_synth.targetGlideFreq = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        g_synth.lastPolyFreq    = g_synth.targetGlideFreq;
        bool firstNote = (g_synth.heldNotes.size() == 1);
        if (firstNote) {
            g_synth.currentGlideFreq = g_synth.targetGlideFreq;
            g_synth.auxEnvs[0].trigger();
            g_synth.auxEnvs[1].trigger();
        }
        if (g_synth.oscA.enabled) TriggerVoicePool(g_synth.voicesA, g_synth.oscA, note, !firstNote);
        if (g_synth.oscB.enabled) TriggerVoicePool(g_synth.voicesB, g_synth.oscB, note, !firstNote);
    } else {
        g_synth.lastPolyFreq = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        g_synth.auxEnvs[0].trigger();
        g_synth.auxEnvs[1].trigger();
        if (g_synth.oscA.enabled) TriggerVoicePool(g_synth.voicesA, g_synth.oscA, note);
        if (g_synth.oscB.enabled) TriggerVoicePool(g_synth.voicesB, g_synth.oscB, note);
    }
}

void NoteOff(int note) {
    std::lock_guard<std::mutex> lock(audioMutex);
    if (note < 0 || note > 127) return;
    g_synth.noteActive[note] = false;

    if (g_synth.monoLegato) {
        auto& held = g_synth.heldNotes;
        auto  it   = std::find(held.begin(), held.end(), note);
        if (it != held.end()) held.erase(it);

        if (held.empty()) {
            for (auto& v : g_synth.voicesA) v.noteOff();
            for (auto& v : g_synth.voicesB) v.noteOff();
            g_synth.auxEnvs[0].release();
            g_synth.auxEnvs[1].release();
        } else {
            int prevNote = held.back();
            g_synth.targetGlideFreq = 440.0f * std::pow(2.0f, (prevNote - 69.0f) / 12.0f);
            g_synth.lastPolyFreq    = g_synth.targetGlideFreq;
        }
    } else {
        for (auto& v : g_synth.voicesA) if (v.getNote() == note) v.noteOff();
        for (auto& v : g_synth.voicesB) if (v.getNote() == note) v.noteOff();

        bool anyActive = false;
        for (const auto& v : g_synth.voicesA) if (v.isActive()) { anyActive = true; break; }
        if (!anyActive) {
            g_synth.auxEnvs[0].release();
            g_synth.auxEnvs[1].release();
        }
    }
}

// === MIDI IO ===

void CALLBACK MidiInProc(HMIDIIN, UINT wMsg, DWORD_PTR, DWORD_PTR dwParam1, DWORD_PTR) {
    if (wMsg != MIM_DATA) return;
    unsigned char status   =  dwParam1        & 0xFF;
    unsigned char note     = (dwParam1 >> 8)  & 0xFF;
    unsigned char velocity = (dwParam1 >> 16) & 0xFF;
    unsigned char type     = status & 0xF0;
    if (type == 0x90 && velocity > 0)
        NoteOn(note);
    else if (type == 0x80 || (type == 0x90 && velocity == 0))
        NoteOff(note);
}

void RefreshMidiPorts() {
    midiPorts.clear();
    UINT numDevs = midiInGetNumDevs();
    for (UINT i = 0; i < numDevs; ++i) {
        MIDIINCAPSA caps;
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            midiPorts.push_back(caps.szPname);
    }
}

void OpenMidiPort(int portIndex) {
    if (hMidiIn) {
        midiInStop(hMidiIn);
        midiInClose(hMidiIn);
        hMidiIn = nullptr;
    }
    if (portIndex >= 0 && portIndex < (int)midiPorts.size()) {
        if (midiInOpen(&hMidiIn, portIndex, (DWORD_PTR)MidiInProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
            midiInStart(hMidiIn);
    }
}

// === MOD MATRIX ===

float GetModSum(int targetId) {
    float sum = 0.0f;
    for (const auto& m : g_synth.modMatrix) {
        if (m.target != targetId) continue;
        float src = 0.0f;
        switch (m.source) {
            case SRC_LFO1: src = g_synth.lfos[0].currentValue; break;
            case SRC_LFO2: src = g_synth.lfos[1].currentValue; break;
            case SRC_ENV1: {
                for (const auto& v : g_synth.voicesA)
                    if (v.isActive()) src = std::max(src, v.env.currentLevel);
                break;
            }
            case SRC_ENV2: src = g_synth.auxEnvs[0].val; break;
            case SRC_ENV3: src = g_synth.auxEnvs[1].val; break;
            default: break;
        }
        sum += src * m.amount;
    }
    return sum;
}

float GetModAmountForUI(int targetId) {
    for (const auto& m : g_synth.modMatrix)
        if (m.source == g_synth.uiModSource && m.target == targetId)
            return m.amount;
    return 0.0f;
}

// === AUDIO RENDER ===

void data_callback(float* pOut, int frameCount) {
    std::lock_guard<std::mutex> lock(audioMutex);
    std::fill(pOut, pOut + frameCount * 2, 0.0f);

    // --- Update LFOs (block-rate) ---
    const float bpmMults[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
    for (int i = 0; i < 2; ++i) {
        g_synth.lfos[i].rateHz = (g_synth.lfoConfig[i].syncMode == 1)
            ? (g_synth.bpm / 60.0f) * bpmMults[g_synth.lfoConfig[i].bpmRateIndex]
            : g_synth.lfoConfig[i].rateHz;
        g_synth.lfos[i].advance(frameCount);
    }

    // --- Block-rate mod sums for oscillators / filter ---
    float modWtA  = GetModSum(TGT_OSCA_WTPOS),  modLevA = GetModSum(TGT_OSCA_LEVEL);
    float modDetA = GetModSum(TGT_OSCA_DETUNE),  modBndA = GetModSum(TGT_OSCA_BLEND);
    float modPitA = GetModSum(TGT_OSCA_PITCH);
    float modWtB  = GetModSum(TGT_OSCB_WTPOS),  modLevB = GetModSum(TGT_OSCB_LEVEL);
    float modDetB = GetModSum(TGT_OSCB_DETUNE),  modBndB = GetModSum(TGT_OSCB_BLEND);
    float modPitB = GetModSum(TGT_OSCB_PITCH);
    float modSubLevel   = GetModSum(TGT_SUB_LEVEL);
    float modNoiseLevel = GetModSum(TGT_NOISE_LEVEL);

    auto clamp = [](float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); };

    float activeCutoff = clamp(g_synth.filter.cutoff * std::pow(2.0f, GetModSum(TGT_FILT_CUTOFF) * 5.0f), 20.0f, 20000.0f);
    float activeRes    = clamp(g_synth.filter.resonance + GetModSum(TGT_FILT_RES) * 5.0f, 0.1f, 10.0f);
    float activeWtA    = clamp(g_synth.oscA.wtPos        + modWtA,  0.0f, 1.0f);
    float activeLevA   = clamp(g_synth.oscA.level        + modLevA, 0.0f, 1.0f);
    float activeDetA   = clamp(g_synth.oscA.unisonDetune + modDetA, 0.0f, 1.0f);
    float activeBndA   = clamp(g_synth.oscA.unisonBlend  + modBndA, 0.0f, 1.0f);
    float activeWtB    = clamp(g_synth.oscB.wtPos        + modWtB,  0.0f, 1.0f);
    float activeLevB   = clamp(g_synth.oscB.level        + modLevB, 0.0f, 1.0f);
    float activeDetB   = clamp(g_synth.oscB.unisonDetune + modDetB, 0.0f, 1.0f);
    float activeBndB   = clamp(g_synth.oscB.unisonBlend  + modBndB, 0.0f, 1.0f);

    // --- Block-rate FX mod: additive over base, base values are NOT modified ---
    float activeDistDrive = clamp(g_synth.distortion.drive + GetModSum(TGT_DIST_DRIVE), 0.0f,  1.0f);
    float activeDistMix   = clamp(g_synth.distortion.mix   + GetModSum(TGT_DIST_MIX),   0.0f,  1.0f);
    float activeDelTime   = clamp(g_synth.delay.time       + GetModSum(TGT_DEL_TIME),   0.01f, 1.5f);
    float activeDelFB     = clamp(g_synth.delay.feedback   + GetModSum(TGT_DEL_FB),     0.0f,  0.95f);
    float activeDelMix    = clamp(g_synth.delay.mix        + GetModSum(TGT_DEL_MIX),    0.0f,  1.0f);
    float activeRevSize   = clamp(g_synth.reverb.roomSize  + GetModSum(TGT_REV_SIZE),   0.0f,  0.98f);
    float activeRevDamp   = clamp(g_synth.reverb.damping   + GetModSum(TGT_REV_DAMP),   0.0f,  1.0f);
    float activeRevMix    = clamp(g_synth.reverb.mix       + GetModSum(TGT_REV_MIX),    0.0f,  1.0f);

    // --- Apply WT position and filter coefficients to all voices ---
    // Voice[0] is always updated (used for filter curve display in UI)
    g_synth.voicesA[0].setWTPos(activeWtA);
    g_synth.voicesA[0].filter.setCoefficients((FilterType)g_synth.filter.type, activeCutoff, activeRes, kSampleRate);
    g_synth.voicesB[0].setWTPos(activeWtB);
    g_synth.voicesB[0].filter.setCoefficients((FilterType)g_synth.filter.type, activeCutoff, activeRes, kSampleRate);
    for (size_t j = 1; j < g_synth.voicesA.size(); ++j) {
        if (g_synth.voicesA[j].isActive()) {
            g_synth.voicesA[j].setWTPos(activeWtA);
            g_synth.voicesA[j].filter.setCoefficients((FilterType)g_synth.filter.type, activeCutoff, activeRes, kSampleRate);
        }
    }
    for (size_t j = 1; j < g_synth.voicesB.size(); ++j) {
        if (g_synth.voicesB[j].isActive()) {
            g_synth.voicesB[j].setWTPos(activeWtB);
            g_synth.voicesB[j].filter.setCoefficients((FilterType)g_synth.filter.type, activeCutoff, activeRes, kSampleRate);
        }
    }

    // Equal-loudness gain compensation for unison voice count
    float gcA        = 1.0f / std::sqrt((float)g_synth.oscA.unisonVoices);
    float gcB        = 1.0f / std::sqrt((float)g_synth.oscB.unisonVoices);
    float masterGain = std::pow(10.0f, g_synth.masterVolumeDb / 20.0f);

    // Glide: 1-pole IIR, time constant ≈ glideTime/3 seconds
    float glideAlpha = (g_synth.glideTime > 0.001f)
        ? 1.0f - std::exp(-1.0f / (kSampleRate * (g_synth.glideTime / 3.0f)))
        : 1.0f;

    // --- Per-sample render loop ---
    for (int i = 0; i < frameCount; ++i) {

        // Advance aux envelopes (ENV2 / ENV3)
        g_synth.auxEnvs[0].process(
            g_synth.envParams[1].attack, g_synth.envParams[1].decay,
            g_synth.envParams[1].sustain, g_synth.envParams[1].release, kSampleRate);
        g_synth.auxEnvs[1].process(
            g_synth.envParams[2].attack, g_synth.envParams[2].decay,
            g_synth.envParams[2].sustain, g_synth.envParams[2].release, kSampleRate);

        // --- Mono-legato pitch glide ---
        if (g_synth.monoLegato && !g_synth.heldNotes.empty()) {
            g_synth.currentGlideFreq += glideAlpha * (g_synth.targetGlideFreq - g_synth.currentGlideFreq);
            g_synth.lastPolyFreq = g_synth.currentGlideFreq;

            auto updateGlideVoices = [&](
                std::vector<WavetableOscillator>& voices,
                const OscConfig& cfg,
                float pitchMod, float activeDet, float activeBnd)
            {
                if (!cfg.enabled) return;
                for (int u = 0; u < cfg.unisonVoices; ++u) {
                    if (!voices[u].isActive()) continue;
                    float spread = (cfg.unisonVoices > 1)
                        ? 2.0f * (float)(cfg.unisonVoices - 1 - u) / (float)(cfg.unisonVoices - 1) - 1.0f
                        : 0.0f;
                    // pitchMod maps ±1.0 → ±24 semitones
                    float mult = std::pow(2.0f,
                        (cfg.octave * 12 + cfg.semi + pitchMod * 24.0f + spread * activeDet * 0.5f) / 12.0f);
                    voices[u].setFrequency(g_synth.currentGlideFreq * mult);
                    voices[u].setPan(spread * activeBnd);
                }
            };
            updateGlideVoices(g_synth.voicesA, g_synth.oscA, modPitA, activeDetA, activeBndA);
            updateGlideVoices(g_synth.voicesB, g_synth.oscB, modPitB, activeDetB, activeBndB);

        } else {
            auto updatePolyVoices = [&](
                std::vector<WavetableOscillator>& voices,
                const OscConfig& cfg,
                float pitchMod)
            {
                if (!cfg.enabled) return;
                for (auto& v : voices) {
                    if (!v.isActive()) continue;
                    float freq = 440.0f * std::pow(2.0f,
                        (v.getNote() + cfg.octave * 12 + cfg.semi + pitchMod * 24.0f - 69.0f) / 12.0f);
                    v.setFrequency(freq);
                }
            };
            updatePolyVoices(g_synth.voicesA, g_synth.oscA, modPitA);
            updatePolyVoices(g_synth.voicesB, g_synth.oscB, modPitB);
        }

        // --- Mix voices ---
        float mixL = 0.0f, mixR = 0.0f;
        float maxEnv = 0.0f;

        auto mixVoices = [&](std::vector<WavetableOscillator>& voices, float activeLevel, float gc) {
            for (auto& v : voices) {
                if (!v.isActive()) continue;
                float p = v.getPan();
                float s = v.getSample() * activeLevel;
                // 0.15f = headroom scale that keeps the stereo sum from clipping with full unison
                mixL   += s * (1.0f - p) * 0.5f * gc * 0.15f;
                mixR   += s * (1.0f + p) * 0.5f * gc * 0.15f;
                maxEnv  = std::max(maxEnv, v.env.currentLevel);
            }
        };
        if (g_synth.oscA.enabled) mixVoices(g_synth.voicesA, activeLevA, gcA);
        if (g_synth.oscB.enabled) mixVoices(g_synth.voicesB, activeLevB, gcB);

        // --- Sub oscillator ---
        if (g_synth.subEnabled && maxEnv > 0.001f) {
            float subF = g_synth.lastPolyFreq * std::pow(2.0f, (float)g_synth.subOctave);
            g_synth.subPhase += subF / kSampleRate;
            if (g_synth.subPhase >= 1.0f) g_synth.subPhase -= 1.0f;
            float subLev = clamp(g_synth.subLevel + modSubLevel, 0.0f, 1.0f);
            float sub = std::sin(g_synth.subPhase * 2.0f * PI) * subLev * maxEnv * 0.2f;
            mixL += sub;
            mixR += sub;
        }

        // --- Noise ---
        if (g_synth.noise.enabled && maxEnv > 0.001f) {
            float activeNoiseLevel = clamp(g_synth.noise.level + modNoiseLevel, 0.0f, 1.0f);
            float ns = g_synth.noise.process() * activeNoiseLevel * maxEnv * 0.15f;
            mixL += ns;
            mixR += ns;
        }

        // --- FX chain (modulated params applied without mutating base values) ---
        if (g_synth.distortion.enabled) {
            float df = 1.0f + activeDistDrive * 10.0f;
            auto distort = [&](float x) {
                float wet = std::atan(x * df) / std::atan(df);
                return x * (1.0f - activeDistMix) + wet * activeDistMix;
            };
            mixL = distort(mixL);
            mixR = distort(mixR);
        }

        if (g_synth.delay.enabled) {
            float savedTime = g_synth.delay.time,
                  savedFB   = g_synth.delay.feedback,
                  savedMix  = g_synth.delay.mix;
            g_synth.delay.time     = activeDelTime;
            g_synth.delay.feedback = activeDelFB;
            g_synth.delay.mix      = activeDelMix;
            g_synth.delay.process(mixL, mixR, kSampleRate);
            g_synth.delay.time     = savedTime;
            g_synth.delay.feedback = savedFB;
            g_synth.delay.mix      = savedMix;
        }

        if (g_synth.reverb.enabled) {
            float savedSize = g_synth.reverb.roomSize,
                  savedDamp = g_synth.reverb.damping,
                  savedMix  = g_synth.reverb.mix;
            g_synth.reverb.roomSize = activeRevSize;
            g_synth.reverb.damping  = activeRevDamp;
            g_synth.reverb.mix      = activeRevMix;
            g_synth.reverb.process(mixL, mixR);
            g_synth.reverb.roomSize = savedSize;
            g_synth.reverb.damping  = savedDamp;
            g_synth.reverb.mix      = savedMix;
        }

        pOut[2 * i]     = mixL * masterGain;
        pOut[2 * i + 1] = mixR * masterGain;

        // --- Scope: triggered on zero-crossing ---
        float mono = (pOut[2*i] + pOut[2*i+1]) * 0.5f;
        if (!g_synth.scopeTriggered && g_synth.lastScopeSample <= 0.0f && mono > 0.0f) {
            g_synth.scopeTriggered = true;
            g_synth.scopeIndex     = 0;
        }
        if (g_synth.scopeTriggered && g_synth.scopeIndex < SCOPE_SIZE)
            g_synth.scopeBuffer[g_synth.scopeIndex++] = mono;
        if (g_synth.scopeIndex >= SCOPE_SIZE)
            g_synth.scopeTriggered = false;
        g_synth.lastScopeSample = mono;

        g_synth.fftRingBuffer[g_synth.fftRingPos] = mono;
        g_synth.fftRingPos = (g_synth.fftRingPos + 1) % FFT_SIZE;
    }
}
