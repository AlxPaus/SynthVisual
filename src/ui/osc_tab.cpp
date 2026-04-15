#include <cmath>
#include <cstdio>
#include "imgui.h"
#include "synth/synth_globals.h"
#include "synth/audio_engine.h"
#include "ui/ui_widgets.h"
#include "ui/osc_tab.h"

void RenderOscillatorBlock(bool isA) {
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

void RenderOscTab() {
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
    ImGui::BeginGroup();

    RenderOscillatorBlock(true);
    RenderOscillatorBlock(false);

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

    ImGui::EndGroup();
}
