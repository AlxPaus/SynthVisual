#define NOMINMAX
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <complex>
#include <vector>
#include <cmath>

#include "wasapi_audio.h"
#include "synth_globals.h"
#include "audio_engine.h"
#include "presets.h"
#include "ui_widgets.h"

// ============================================================
// PC keyboard → MIDI note input
// ============================================================

static void HandlePcKeyboard(GLFWwindow* window) {
    for (const auto& k : pcKeys) {
        bool isDown = (glfwGetKey(window, k.key) == GLFW_PRESS);
        if (isDown && g_synth.pcKeyNote[k.key] == -1) {
            int note = ((g_synth.keyboardOctave + 1) * 12) + k.offset;
            note = std::max(0, std::min(127, note));
            g_synth.pcKeyNote[k.key] = note;
            g_synth.pcNoteHeld[note] = true;
            NoteOn(note);
        } else if (!isDown && g_synth.pcKeyNote[k.key] != -1) {
            int note = g_synth.pcKeyNote[k.key];
            g_synth.pcNoteHeld[note] = false;
            g_synth.pcKeyNote[k.key] = -1;
            NoteOff(note);
        }
    }
}

// ============================================================
// Spectrum (FFT with Hann window, log-scale smoothing)
// ============================================================

static void UpdateSpectrum() {
    std::vector<std::complex<float>> fftData(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i) {
        int   idx = (g_synth.fftRingPos + i) % FFT_SIZE;
        float win = 0.5f * (1.0f - std::cos(2.0f * PI * i / (FFT_SIZE - 1)));
        fftData[i] = std::complex<float>(g_synth.fftRingBuffer[idx] * win, 0.0f);
    }
    computeFFT(fftData);
    for (int i = 0; i < FFT_SIZE / 2; ++i) {
        float mag = std::abs(fftData[i]) / (FFT_SIZE / 2.0f);
        float db  = 20.0f * std::log10(std::max(1e-6f, mag));
        float val = std::max(0.0f, std::min(1.0f, (db + 70.0f) / 70.0f));
        // Low-pass smoothing: 80% old, 20% new
        g_synth.spectrumSmooth[i] = g_synth.spectrumSmooth[i] * 0.8f + val * 0.2f;
    }
}

// ============================================================
// Oscillator A or B block — shared renderer to avoid duplication
// ============================================================

static void RenderOscillatorBlock(bool isA) {
    OscConfig& cfg   = isA ? g_synth.oscA : g_synth.oscB;
    const char* lbl  = isA ? "OSC A" : "OSC B";
    int tgtWtPos     = isA ? TGT_OSCA_WTPOS  : TGT_OSCB_WTPOS;
    int tgtDetune    = isA ? TGT_OSCA_DETUNE : TGT_OSCB_DETUNE;
    int tgtBlend     = isA ? TGT_OSCA_BLEND  : TGT_OSCB_BLEND;
    int tgtLevel     = isA ? TGT_OSCA_LEVEL  : TGT_OSCB_LEVEL;
    const char* uniId = isA ? "##UniA" : "##UniB";

    ImGui::PushID(isA ? "OSC_A" : "OSC_B");
    ImGui::BeginGroup();

    ImGui::Checkbox(lbl, &cfg.enabled);
    ImGui::SameLine();
    ImGui::PushItemWidth(150);
    if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
        cfg.tableIndex = (cfg.tableIndex <= 0)
            ? (int)g_synth.wavetables.tables.size() - 1 : cfg.tableIndex - 1;
        UpdateOscillatorsTable();
    }
    ImGui::SameLine();
    ImGui::Button(g_synth.wavetables.tables[cfg.tableIndex].name.c_str(), ImVec2(150, 0));
    ImGui::SameLine();
    if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
        cfg.tableIndex = (cfg.tableIndex >= (int)g_synth.wavetables.tables.size() - 1)
            ? 0 : cfg.tableIndex + 1;
        UpdateOscillatorsTable();
    }
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 20);
    if (DrawKnob("WT POS", &cfg.wtPos, 0.0f, 1.0f, "%.2f", tgtWtPos))
        UpdateOscillatorsTable();

    ImGui::SameLine(0, 10);
    ImGui::BeginGroup();
    ImGui::Text("PITCH");
    ImGui::PushItemWidth(40);
    ImGui::DragInt("OCT", &cfg.octave, 0.1f, -4, 4);
    ImGui::DragInt("SEM", &cfg.semi,   0.1f, -12, 12);
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::BeginGroup();
    ImGui::Text("UNISON");
    ImGui::PushItemWidth(40);
    ImGui::DragInt(uniId, &cfg.unisonVoices, 0.2f, 1, 16);
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SameLine(0, 10); DrawKnob("Detune", &cfg.unisonDetune, 0.0f, 1.0f, "%.2f", tgtDetune);
    ImGui::SameLine(0, 10); DrawKnob("Blend",  &cfg.unisonBlend,  0.0f, 1.0f, "%.2f", tgtBlend);
    ImGui::SameLine(0, 10); DrawKnob("Level",  &cfg.level,        0.0f, 1.0f, "%.2f", tgtLevel);

    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::Separator();
}

// ============================================================
// Top bar: keyboard octave, MIDI in, preset, master volume
// ============================================================

static void RenderTopBar(std::string& presetName) {
    ImGui::BeginGroup();
    ImGui::Text("KEYBOARD");
    ImGui::PushItemWidth(100);
    ImGui::DragInt("OCTAVE", &g_synth.keyboardOctave, 0.1f, 1, 7);
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    ImGui::Text("MIDI IN");
    ImGui::PushItemWidth(150);
    const char* midiLabel = (selectedMidiPort >= 0 && selectedMidiPort < (int)midiPorts.size())
        ? midiPorts[selectedMidiPort].c_str() : "None";
    if (ImGui::BeginCombo("##MidiIn", midiLabel)) {
        if (ImGui::Selectable("None", selectedMidiPort == -1)) { selectedMidiPort = -1; OpenMidiPort(-1); }
        for (int i = 0; i < (int)midiPorts.size(); ++i) {
            if (ImGui::Selectable(midiPorts[i].c_str(), selectedMidiPort == i)) {
                selectedMidiPort = i;
                OpenMidiPort(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    ImGui::Text("PRESET: %s", presetName.c_str());
    if (ImGui::Button("SAVE")) {
        std::string path = SaveFileDialog();
        if (!path.empty()) { SavePreset(path); presetName = path.substr(path.find_last_of("/\\") + 1); }
    }
    ImGui::SameLine();
    if (ImGui::Button("LOAD")) {
        std::string path = OpenFileDialog();
        if (!path.empty()) { LoadPreset(path); presetName = path.substr(path.find_last_of("/\\") + 1); }
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    ImGui::Text("MASTER");
    DrawKnob("Vol", &g_synth.masterVolumeDb, -36.0f, 12.0f, "%.1f dB", TGT_NONE);
    ImGui::EndGroup();
}

// ============================================================
// OSC tab
// ============================================================

static void RenderOscTab() {
    // Left column: sub osc, noise, voicing
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255));
    ImGui::BeginChild("LeftTools", ImVec2(180, 260), true);

    ImGui::Text("SUB OSC");
    ImGui::Checkbox("##SubOn", &g_synth.subEnabled);
    ImGui::SameLine();
    ImGui::PushItemWidth(60);
    ImGui::DragInt("Oct", &g_synth.subOctave, 0.1f, -4, 0);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    DrawKnob("Lev##Sub", &g_synth.subLevel, 0.0f, 1.0f, "%.2f", TGT_SUB_LEVEL);

    ImGui::Separator();
    ImGui::Text("NOISE");
    ImGui::Checkbox("##NoiseOn", &g_synth.noise.enabled);
    ImGui::SameLine(0, 5);
    ImGui::PushItemWidth(80);
    const char* noiseTypes[] = { "White", "Pink", "Brown" };
    ImGui::Combo("##NType", &g_synth.noise.type, noiseTypes, 3);
    ImGui::PopItemWidth();
    ImGui::SameLine(0, 5);
    DrawKnob("Lev##Noise", &g_synth.noise.level, 0.0f, 1.0f, "%.2f", TGT_NOISE_LEVEL);

    ImGui::Separator();
    ImGui::Text("VOICING");
    ImGui::Checkbox("Legato", &g_synth.monoLegato);
    ImGui::SameLine(0, 20);
    DrawKnob("Glide", &g_synth.glideTime, 0.0f, 1.0f, "%.2fs", TGT_NONE);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImGui::SameLine(0, 15);

    // Right column: oscillators, envelopes, filter, LFOs, spectrum, 3D view
    ImGui::BeginGroup();

    RenderOscillatorBlock(true);
    RenderOscillatorBlock(false);

    // Envelopes
    ImGui::BeginGroup();
    ImGui::BeginTabBar("EnvTabs");
    for (int i = 0; i < 3; ++i) {
        char envName[16];
        snprintf(envName, sizeof(envName), i == 0 ? "ENV 1 (AMP)" : "ENV %d", i + 1);
        if (!ImGui::BeginTabItem(envName)) continue;

        g_synth.uiModSource = SRC_ENV1 + i;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
        ImGui::Button("(+) DRAG MOD", ImVec2(90, 25));
        ImGui::PopStyleColor();
        if (ImGui::BeginDragDropSource()) {
            int src = SRC_ENV1 + i;
            ImGui::SetDragDropPayload("MOD_SRC", &src, sizeof(int));
            ImGui::Text("Assign ENV %d", i + 1);
            ImGui::EndDragDropSource();
        }

        ImGui::SameLine(0, 20);
        EnvParams& ep = g_synth.envParams[i];
        bool envChanged = false;
        if (DrawKnob("A", &ep.attack,  0.001f, 5.0f, "%.2fs", TGT_NONE)) envChanged = true;
        ImGui::SameLine(0, 10);
        if (DrawKnob("D", &ep.decay,   0.001f, 5.0f, "%.2fs", TGT_NONE)) envChanged = true;
        ImGui::SameLine(0, 10);
        if (DrawKnob("S", &ep.sustain, 0.0f,   1.0f, "%.2f",  TGT_NONE)) envChanged = true;
        ImGui::SameLine(0, 10);
        if (DrawKnob("R", &ep.release, 0.001f, 5.0f, "%.2fs", TGT_NONE)) envChanged = true;
        if (envChanged) UpdateEnvelopes();

        // Envelope shape graph
        ImVec2 gPos  = ImGui::GetCursorScreenPos();
        ImVec2 gSize(260.0f, 60.0f);
        ImGui::InvisibleButton("##EnvGraph", gSize);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(gPos, ImVec2(gPos.x + gSize.x, gPos.y + gSize.y), IM_COL32(30, 30, 35, 255));

        float tt = 10.0f;
        float pxA = (ep.attack  / tt) * gSize.x * 2.0f;
        float pxD = (ep.decay   / tt) * gSize.x * 2.0f;
        float pxS = 40.0f;
        float pxR = (ep.release / tt) * gSize.x * 2.0f;
        ImVec2 p0(gPos.x,           gPos.y + gSize.y);
        ImVec2 p1(p0.x + pxA,       gPos.y);
        ImVec2 p2(p1.x + pxD,       gPos.y + gSize.y - ep.sustain * gSize.y);
        ImVec2 p3(p2.x + pxS,       p2.y);
        ImVec2 p4(p3.x + pxR,       gPos.y + gSize.y);
        ImU32 lineClr = IM_COL32(0, 200, 255, 255);
        dl->AddLine(p0, p1, lineClr, 2.0f); dl->AddLine(p1, p2, lineClr, 2.0f);
        dl->AddLine(p2, p3, lineClr, 2.0f); dl->AddLine(p3, p4, lineClr, 2.0f);
        dl->AddCircleFilled(p1, 4.0f, IM_COL32(255, 255, 255, 255));
        dl->AddCircleFilled(p2, 4.0f, IM_COL32(255, 255, 255, 255));
        dl->AddCircleFilled(p4, 4.0f, IM_COL32(255, 255, 255, 255));

        // Moving dot showing current envelope position
        float lvl = 0.0f;
        int envState = 0;
        if (i == 0) {
            for (int v = 0; v < MAX_VOICES; ++v) {
                if (g_synth.voicesA[v].isActive() && g_synth.voicesA[v].env.currentLevel > lvl) {
                    lvl      = g_synth.voicesA[v].env.currentLevel;
                    envState = (int)g_synth.voicesA[v].env.state;
                }
            }
        } else {
            lvl      = g_synth.auxEnvs[i-1].val;
            envState = g_synth.auxEnvs[i-1].state;
        }

        if (envState != 0) {
            float dotY = gPos.y + gSize.y - lvl * gSize.y;
            float dotX = gPos.x;
            float sus  = ep.sustain;
            if      (envState == 1) dotX = p0.x + pxA * lvl;
            else if (envState == 2) { float range = 1.0f - sus; dotX = p1.x + pxD * ((range > 0.001f) ? (1.0f - lvl) / range : 1.0f); }
            else if (envState == 3) dotX = p2.x + pxS * 0.5f;
            else if (envState == 4) { float t = (sus > 0.001f) ? 1.0f - (lvl / sus) : 1.0f; dotX = p3.x + pxR * std::max(0.0f, std::min(1.0f, t)); }
            dl->AddCircleFilled(ImVec2(dotX, dotY), 4.5f, IM_COL32(255, 215, 0, 255));
        }

        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::EndGroup();

    ImGui::SameLine(0, 40);

    // Filter
    ImGui::PushID("FILTER1");
    ImGui::BeginGroup();
    ImGui::Text("FILTER");
    ImGui::BeginGroup();
    bool filterChanged = false;
    if (ImGui::Checkbox("##FiltEnable", &g_synth.filter.enabled)) filterChanged = true;
    ImGui::SameLine();
    ImGui::PushItemWidth(150);
    const char* filterTypes[] = { "Low Pass 12dB", "High Pass 12dB", "Band Pass 12dB" };
    if (ImGui::Combo("##FiltType", &g_synth.filter.type, filterTypes, 3)) filterChanged = true;
    ImGui::PopItemWidth();
    ImGui::SameLine(0, 20);
    if (DrawKnob("Cutoff", &g_synth.filter.cutoff,    20.0f, 20000.0f, "%.0f Hz", TGT_FILT_CUTOFF, true)) filterChanged = true;
    ImGui::SameLine(0, 10);
    if (DrawKnob("Res",    &g_synth.filter.resonance,  0.1f,    10.0f, "%.2f Q",  TGT_FILT_RES)) filterChanged = true;
    if (filterChanged) UpdateFilters();
    ImGui::EndGroup();

    ImGui::SameLine(0, 20);
    ImVec2 fPos  = ImGui::GetCursorScreenPos();
    ImVec2 fSize(250.0f, 80.0f);
    ImGui::InvisibleButton("##FiltGraph", fSize);
    ImDrawList* fd = ImGui::GetWindowDrawList();
    fd->AddRectFilled(fPos, ImVec2(fPos.x + fSize.x, fPos.y + fSize.y), IM_COL32(30, 30, 35, 255));
    if (g_synth.filter.enabled) {
        std::vector<ImVec2> pts;
        for (int i = 0; i < (int)fSize.x; ++i) {
            float freq  = 20.0f * std::pow(1000.0f, (float)i / (fSize.x - 1.0f));
            float magDb = 20.0f * std::log10(std::max(0.0001f, g_synth.voicesA[0].filter.getMagnitude(freq, kSampleRate)));
            pts.push_back(ImVec2(fPos.x + i, fPos.y + std::max(0.0f, std::min(1.0f, 1.0f - (magDb + 24.0f) / 42.0f)) * fSize.y));
        }
        for (size_t i = 0; i < pts.size() - 1; ++i)
            fd->AddLine(pts[i], pts[i+1], IM_COL32(0, 255, 100, 255), 2.0f);

        float liveCutoff = std::max(20.0f, std::min(20000.0f,
            g_synth.filter.cutoff * std::pow(2.0f, GetModSum(TGT_FILT_CUTOFF) * 5.0f)));
        float xCut = fPos.x + (std::log(liveCutoff / 20.0f) / std::log(1000.0f)) * fSize.x;
        if (xCut >= fPos.x && xCut <= fPos.x + fSize.x) {
            fd->AddLine(ImVec2(xCut, fPos.y), ImVec2(xCut, fPos.y + fSize.y), IM_COL32(255, 255, 255, 50), 1.0f);
            float yNorm = 1.0f - (20.0f * std::log10(std::max(0.0001f, g_synth.voicesA[0].filter.getMagnitude(liveCutoff, kSampleRate))) + 24.0f) / 42.0f;
            fd->AddCircleFilled(ImVec2(xCut, fPos.y + std::max(0.0f, std::min(1.0f, yNorm)) * fSize.y), 4.0f, IM_COL32(255, 255, 255, 255));
        }
    } else {
        float yFlat = fPos.y + (1.0f - 24.0f / 42.0f) * fSize.y;
        fd->AddLine(ImVec2(fPos.x, yFlat), ImVec2(fPos.x + fSize.x, yFlat), IM_COL32(100, 100, 100, 255), 2.0f);
    }
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::Separator();

    // LFOs
    ImGui::BeginGroup();
    ImGui::BeginTabBar("LfoTabs");
    for (int i = 0; i < 2; ++i) {
        char lfoName[16];
        snprintf(lfoName, sizeof(lfoName), "LFO %d", i + 1);
        if (!ImGui::BeginTabItem(lfoName)) continue;

        g_synth.uiModSource = SRC_LFO1 + i;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.8f, 1.0f));
        ImGui::Button("(+) DRAG MOD", ImVec2(90, 25));
        ImGui::PopStyleColor();
        if (ImGui::BeginDragDropSource()) {
            int src = SRC_LFO1 + i;
            ImGui::SetDragDropPayload("MOD_SRC", &src, sizeof(int));
            ImGui::Text("Assign LFO %d", i + 1);
            ImGui::EndDragDropSource();
        }

        ImGui::SameLine(0, 20);
        ImGui::PushItemWidth(80);
        const char* lfoShapes[] = { "Sine", "Triangle", "Saw", "Square" };
        ImGui::Combo("Shape", &g_synth.lfos[i].shape, lfoShapes, 4);
        ImGui::SameLine();
        const char* syncModes[] = { "Hz", "BPM" };
        ImGui::Combo("Sync", &g_synth.lfoConfig[i].syncMode, syncModes, 2);
        if (g_synth.lfoConfig[i].syncMode == 1) {
            ImGui::SameLine();
            ImGui::DragFloat("BPM", &g_synth.bpm, 1.0f, 20.0f, 300.0f, "%.1f");
            ImGui::SameLine();
            const char* rates[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
            ImGui::Combo("Rate", &g_synth.lfoConfig[i].bpmRateIndex, rates, 6);
        } else {
            ImGui::SameLine();
            ImGui::SliderFloat("Rate", &g_synth.lfoConfig[i].rateHz, 0.05f, 20.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic);
        }
        ImGui::SameLine();
        ImGui::ProgressBar((g_synth.lfos[i].currentValue + 1.0f) / 2.0f, ImVec2(100, 20), "Phase");
        ImGui::PopItemWidth();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::EndGroup();

    ImGui::SameLine(0, 30);

    // Spectrum display
    ImGui::BeginGroup();
    ImGui::Text("EQ SPECTRUM");
    ImVec2 specPos = ImGui::GetCursorScreenPos();
    float  specW   = ImGui::GetContentRegionAvail().x - 10.0f;
    float  specH   = 80.0f;
    ImGui::InvisibleButton("##Spectrum", ImVec2(specW, specH));
    ImDrawList* sd = ImGui::GetWindowDrawList();
    sd->AddRectFilled(specPos, ImVec2(specPos.x + specW, specPos.y + specH), IM_COL32(20, 20, 25, 255));
    for (int x = 0; x < (int)specW - 1; x += 3) {
        float freq = 20.0f * std::pow(1000.0f, (float)x / specW);
        int   bin  = std::max(0, std::min((int)(freq * (FFT_SIZE / kSampleRate)), FFT_SIZE / 2 - 1));
        float val  = g_synth.spectrumSmooth[bin];
        sd->AddLine(ImVec2(specPos.x + x, specPos.y + specH),
                    ImVec2(specPos.x + x, specPos.y + specH - val * specH),
                    IM_COL32(255, 100, 200, 200), 2.0f);
    }
    ImGui::EndGroup();
    ImGui::Separator();

    // 3D wavetable views for both oscillators
    ImVec2 pos3d  = ImGui::GetCursorScreenPos();
    float  totalW = ImGui::GetContentRegionAvail().x;
    float  height = 160.0f;
    ImGui::InvisibleButton("##3DViewSplit", ImVec2(totalW, height));
    ImDrawList* dl3d = ImGui::GetWindowDrawList();

    Draw3DTable(dl3d, pos3d,
        totalW / 2.0f - 5.0f, height,
        g_synth.wavetables.tables[g_synth.oscA.tableIndex],
        std::max(0.0f, std::min(1.0f, g_synth.oscA.wtPos + GetModSum(TGT_OSCA_WTPOS))),
        std::max(0.0f, std::min(1.0f, g_synth.oscA.level + GetModSum(TGT_OSCA_LEVEL))),
        g_synth.voicesA[0].getWavetableData(), g_synth.oscA.enabled);

    Draw3DTable(dl3d, ImVec2(pos3d.x + totalW / 2.0f + 5.0f, pos3d.y),
        totalW / 2.0f - 5.0f, height,
        g_synth.wavetables.tables[g_synth.oscB.tableIndex],
        std::max(0.0f, std::min(1.0f, g_synth.oscB.wtPos + GetModSum(TGT_OSCB_WTPOS))),
        std::max(0.0f, std::min(1.0f, g_synth.oscB.level + GetModSum(TGT_OSCB_LEVEL))),
        g_synth.voicesB[0].getWavetableData(), g_synth.oscB.enabled);

    ImGui::EndGroup(); // end right column
}

// ============================================================
// DRAW tab (wavetable shape editor)
// ============================================================

static void RenderDrawTab() {
    static std::vector<float> customWave(TABLE_SIZE, 0.0f);
    static float lastMouseX = -1.0f;
    static float lastMouseY = -1.0f;

    ImGui::BeginGroup();
    if (ImGui::Button("CLEAR", ImVec2(100, 30)))
        std::fill(customWave.begin(), customWave.end(), 0.0f);

    ImGui::SameLine();
    if (ImGui::Button("SMOOTH BRUSH", ImVec2(120, 30))) {
        std::vector<float> temp = customWave;
        for (int i = 1; i < TABLE_SIZE - 1; ++i)
            customWave[i] = (temp[i-1] + temp[i] + temp[i+1]) / 3.0f;
    }

    ImGui::SameLine(0, 50);
    if (ImGui::Button("APPLY TO OSC A (Current Frame)", ImVec2(250, 30))) {
        std::lock_guard<std::mutex> lock(audioMutex);
        int frameIdx = (int)(g_synth.oscA.wtPos
            * (g_synth.wavetables.tables[g_synth.oscA.tableIndex].frames.size() - 1));
        g_synth.wavetables.tables[g_synth.oscA.tableIndex].frames[frameIdx] = customWave;
        UpdateOscillatorsTable();
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 10));

    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y -= 10.0f;
    ImGui::InvisibleButton("##canvas", canvasSize);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 25, 30, 255));
    dl->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(100, 100, 100, 255));
    float midY = canvasPos.y + canvasSize.y * 0.5f;
    dl->AddLine(ImVec2(canvasPos.x, midY), ImVec2(canvasPos.x + canvasSize.x, midY), IM_COL32(80, 80, 80, 255));

    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        ImVec2 mp = ImGui::GetMousePos();
        float x   = mp.x - canvasPos.x;
        float y   = mp.y - canvasPos.y;
        int   idx = std::max(0, std::min(TABLE_SIZE - 1, (int)((x / canvasSize.x) * (TABLE_SIZE - 1))));
        float val = std::max(-1.0f, std::min(1.0f, 1.0f - 2.0f * (y / canvasSize.y)));

        if (lastMouseX >= 0.0f) {
            int   lastIdx = std::max(0, std::min(TABLE_SIZE - 1, (int)((lastMouseX / canvasSize.x) * (TABLE_SIZE - 1))));
            float lastVal = std::max(-1.0f, std::min(1.0f, 1.0f - 2.0f * (lastMouseY / canvasSize.y)));
            int start = std::min(lastIdx, idx), end = std::max(lastIdx, idx);
            for (int i = start; i <= end; ++i) {
                customWave[i] = (start == end) ? val
                    : lastVal + (float)(i - lastIdx) / (float)(idx - lastIdx) * (val - lastVal);
            }
        } else {
            customWave[idx] = val;
        }
        lastMouseX = x; lastMouseY = y;
    } else {
        lastMouseX = -1.0f; lastMouseY = -1.0f;
    }

    for (int i = 0; i < TABLE_SIZE - 1; ++i) {
        float x1 = canvasPos.x + ((float)i       / (TABLE_SIZE - 1)) * canvasSize.x;
        float x2 = canvasPos.x + ((float)(i + 1) / (TABLE_SIZE - 1)) * canvasSize.x;
        float y1 = midY - customWave[i]     * (canvasSize.y * 0.5f);
        float y2 = midY - customWave[i + 1] * (canvasSize.y * 0.5f);
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(255, 150, 0, 255), 2.0f);
    }
}

// ============================================================
// FX tab
// ============================================================

static void RenderFxPanel(const char* id, const char* title, bool& enabled) {
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255));
    ImGui::BeginChild(id, ImVec2(0, 100), true);
    ImGui::Checkbox(title, &enabled);
}

static void EndFxPanel() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
}

static void RenderFxTab() {
    RenderFxPanel("FX_DIST", "DISTORTION", g_synth.distortion.enabled);
    ImGui::SameLine(150); DrawKnob("Drive", &g_synth.distortion.drive, 0.0f, 1.0f, "%.2f", TGT_DIST_DRIVE);
    ImGui::SameLine(250); DrawKnob("Mix",   &g_synth.distortion.mix,   0.0f, 1.0f, "%.2f", TGT_DIST_MIX);
    EndFxPanel();

    RenderFxPanel("FX_DELAY", "DELAY", g_synth.delay.enabled);
    ImGui::SameLine(150); DrawKnob("Time",  &g_synth.delay.time,     0.01f, 1.5f,  "%.2fs", TGT_DEL_TIME);
    ImGui::SameLine(250); DrawKnob("Fback", &g_synth.delay.feedback, 0.0f,  0.95f, "%.2f",  TGT_DEL_FB);
    ImGui::SameLine(350); DrawKnob("Mix",   &g_synth.delay.mix,      0.0f,  1.0f,  "%.2f",  TGT_DEL_MIX);
    EndFxPanel();

    RenderFxPanel("FX_REV", "REVERB", g_synth.reverb.enabled);
    ImGui::SameLine(150); DrawKnob("Size", &g_synth.reverb.roomSize, 0.0f, 0.98f, "%.2f", TGT_REV_SIZE);
    ImGui::SameLine(250); DrawKnob("Damp", &g_synth.reverb.damping,  0.0f, 1.0f,  "%.2f", TGT_REV_DAMP);
    ImGui::SameLine(350); DrawKnob("Mix",  &g_synth.reverb.mix,      0.0f, 1.0f,  "%.2f", TGT_REV_MIX);
    EndFxPanel();

    ImGui::Dummy(ImVec2(0.0f, 60.0f));
}

// ============================================================
// MOD MATRIX tab
// ============================================================

static void RenderModMatrixTab() {
    static const char* srcNames[] = { "None", "LFO 1", "LFO 2", "ENV 1", "ENV 2", "ENV 3" };
    static const char* tgtNames[] = {
        "None",
        "Osc A WT Pos", "Osc A Pitch", "Osc A Level", "Osc A Detune", "Osc A Blend",
        "Osc B WT Pos", "Osc B Pitch", "Osc B Level", "Osc B Detune", "Osc B Blend",
        "Filter Cutoff", "Filter Res",
        "Sub Level", "Noise Level",
        "Dist Drive", "Dist Mix",
        "Delay Time", "Delay FB", "Delay Mix",
        "Reverb Size", "Reverb Damp", "Reverb Mix"
    };

    ImGui::Columns(4, "modmatrix_cols");
    ImGui::Separator();
    ImGui::Text("SOURCE");    ImGui::NextColumn();
    ImGui::Text("AMOUNT");    ImGui::NextColumn();
    ImGui::Text("DESTINATION"); ImGui::NextColumn();
    ImGui::Text("ACTION");    ImGui::NextColumn();
    ImGui::Separator();

    for (int i = 0; i < (int)g_synth.modMatrix.size(); ++i) {
        ImGui::PushID(i);
        ImGui::Text("%s", srcNames[g_synth.modMatrix[i].source]);
        ImGui::NextColumn();
        ImGui::SliderFloat("##amt", &g_synth.modMatrix[i].amount, -1.0f, 1.0f, "%.2f");
        ImGui::NextColumn();
        ImGui::Text("%s", tgtNames[g_synth.modMatrix[i].target]);
        ImGui::NextColumn();
        if (ImGui::Button("REMOVE", ImVec2(80, 0))) {
            std::lock_guard<std::mutex> lock(audioMutex);
            g_synth.modMatrix.erase(g_synth.modMatrix.begin() + i);
            --i;
        }
        ImGui::NextColumn();
        ImGui::PopID();
    }

    ImGui::Columns(1);
    ImGui::Separator();
}

// ============================================================
// Oscilloscope
// ============================================================

static void RenderScopeSection() {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float  w   = ImGui::GetContentRegionAvail().x;
    float  h   = 60.0f;
    ImGui::InvisibleButton("##MainScope", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(15, 15, 20, 255));
    float midY = pos.y + h * 0.5f;
    dl->AddLine(ImVec2(pos.x, midY), ImVec2(pos.x + w, midY), IM_COL32(40, 40, 50, 255), 1.0f);

    for (int i = 0; i < SCOPE_SIZE - 1; ++i) {
        float x1 = pos.x + ((float)i       / (SCOPE_SIZE - 1)) * w;
        float x2 = pos.x + ((float)(i + 1) / (SCOPE_SIZE - 1)) * w;
        float y1 = midY - std::max(-1.0f, std::min(1.0f, g_synth.scopeBuffer[i]))     * (h * 0.45f);
        float y2 = midY - std::max(-1.0f, std::min(1.0f, g_synth.scopeBuffer[i + 1])) * (h * 0.45f);
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 255, 150, 255), 1.5f);
    }
}

// ============================================================
// Piano keyboard
// ============================================================

static void RenderPianoSection(int& mouseHeldNote) {
    const float whiteW  = 36.0f, whiteH = 150.0f;
    const float blackW  = 22.0f, blackH = 90.0f;
    const float spacing = 1.0f;
    const int   startOct = 2, endOct = 7;

    ImGui::BeginChild("PianoScroll", ImVec2(0, 200), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 origin = ImGui::GetCursorPos();
    int currentMouseNote = -1;

    // White keys
    int whiteIndex = 0;
    for (int oct = startOct; oct <= endOct; ++oct) {
        for (int note = 0; note < 12; ++note) {
            if (note == 1 || note == 3 || note == 6 || note == 8 || note == 10) continue;
            int midi = (oct + 1) * 12 + note;
            ImGui::SetCursorPos(ImVec2(origin.x + whiteIndex * (whiteW + spacing), origin.y));
            ImGui::PushID(midi);
            if (PianoKey("##w", false, g_synth.noteActive[midi], ImVec2(whiteW, whiteH)))
                currentMouseNote = midi;
            ImGui::PopID();
            if (note == 0) {
                ImGui::SetCursorPos(ImVec2(origin.x + whiteIndex * (whiteW + spacing) + 8, origin.y + whiteH - 20));
                ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.2f, 1.0f), "C%d", oct);
            }
            ++whiteIndex;
        }
    }

    // Black keys (overlaid on top)
    whiteIndex = 0;
    for (int oct = startOct; oct <= endOct; ++oct) {
        for (int note = 0; note < 12; ++note) {
            bool isBlack = (note == 1 || note == 3 || note == 6 || note == 8 || note == 10);
            if (!isBlack) { ++whiteIndex; continue; }
            int   midi    = (oct + 1) * 12 + note;
            float offsetX = origin.x + whiteIndex * (whiteW + spacing) - blackW / 2.0f - spacing / 2.0f;
            ImGui::SetCursorPos(ImVec2(offsetX, origin.y));
            ImGui::PushID(midi);
            if (PianoKey("##b", true, g_synth.noteActive[midi], ImVec2(blackW, blackH)))
                currentMouseNote = midi;
            ImGui::PopID();
        }
    }

    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + whiteH + 10));
    ImGui::EndChild();

    // Release previous mouse-held note if mouse moved away
    if (mouseHeldNote != -1 && mouseHeldNote != currentMouseNote) {
        if (!g_synth.pcNoteHeld[mouseHeldNote])  // don't release if PC keyboard is still holding it
            NoteOff(mouseHeldNote);
        mouseHeldNote = -1;
    }
    if (currentMouseNote != -1 && currentMouseNote != mouseHeldNote) {
        NoteOn(currentMouseNote);
        mouseHeldNote = currentMouseNote;
    }
}

// ============================================================
// main
// ============================================================

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(1280, 950, "Osci: Wave Editor & Synth", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    ImGui::StyleColorsDark();

    for (int i = 0; i < MAX_VOICES; ++i) {
        g_synth.voicesA.emplace_back(kSampleRate);
        g_synth.voicesB.emplace_back(kSampleRate);
    }
    UpdateOscillatorsTable();
    UpdateEnvelopes();
    UpdateFilters();
    RefreshMidiPorts();

    WasapiAudioDriver audioDriver;
    if (!audioDriver.init(data_callback)) {
        std::cerr << "Failed to initialize audio\n";
        return 1;
    }
    audioDriver.start();

    int         mouseHeldNote = -1;
    std::string currentPreset = "Init";

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        HandlePcKeyboard(window);
        UpdateSpectrum();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Synth UI", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

        RenderTopBar(currentPreset);
        ImGui::Separator();

        if (ImGui::BeginTabBar("SerumTabs")) {
            if (ImGui::BeginTabItem("OSC"))        { RenderOscTab();       ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("DRAW"))       { RenderDrawTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("FX"))         { RenderFxTab();        ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("MOD MATRIX")) { RenderModMatrixTab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        RenderScopeSection();
        ImGui::Separator();
        RenderPianoSection(mouseHeldNote);

        ImGui::End();
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    audioDriver.stop();
    if (hMidiIn) { midiInStop(hMidiIn); midiInClose(hMidiIn); }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
