#define NOMINMAX 
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib") 

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>

// Подключаем наши новые модули
#include "wasapi_audio.h" 
#include "synth_globals.h"
#include "audio_engine.h"
#include "presets.h"
#include "ui_widgets.h"

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0); glfwWindowHint(GLFW_SAMPLES, 4); 
    GLFWwindow* window = glfwCreateWindow(1280, 950, "Osci: Wave Editor & Refactored Core", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window); glfwSwapInterval(1);
    
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui_ImplGlfw_InitForOpenGL(window, true); ImGui_ImplOpenGL3_Init("#version 130");
    ImGui::StyleColorsDark();
    
    // Инициализация голосов (128 полифония)
    for(int i=0; i<MAX_VOICES; ++i) { voicesA.emplace_back(44100.0f); voicesB.emplace_back(44100.0f); }
    UpdateOscillatorsTable(); UpdateEnvelopes(); UpdateFilters();
    for (int i=0; i<512; i++) keysPressedMidi[i] = -1; 

    RefreshMidiPorts();

    // Запуск звука
    WasapiAudioDriver audioDriver;
    if (!audioDriver.init(data_callback)) { std::cerr << "Failed to initialize Windows Audio!" << std::endl; return 1; }
    audioDriver.start();
    
    int mouseHeldNote = -1; 
    std::string currentPresetName = "Init";

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        // Чтение клавиатуры ПК
        for (const auto& k : pcKeys) {
            bool isDown = (glfwGetKey(window, k.key) == GLFW_PRESS);
            if (isDown && keysPressedMidi[k.key] == -1) { 
                int midiNote = ((keyboardOctave + 1) * 12) + k.offset; if (midiNote < 0) midiNote = 0; if (midiNote > 127) midiNote = 127;
                keysPressedMidi[k.key] = midiNote; NoteOn(midiNote); 
            } 
            else if (!isDown && keysPressedMidi[k.key] != -1) { 
                NoteOff(keysPressedMidi[k.key]); keysPressedMidi[k.key] = -1; 
            }
        }

        // Обновление Спектра (FFT) для UI
        std::vector<std::complex<float>> fftData(FFT_SIZE);
        for(int i=0; i<FFT_SIZE; ++i) {
            int idx = (fftRingPos + i) % FFT_SIZE; float windowFunc = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (FFT_SIZE - 1)));
            fftData[i] = std::complex<float>(fftRingBuffer[idx] * windowFunc, 0.0f);
        }
        computeFFT(fftData); 
        for(int i=0; i<FFT_SIZE/2; ++i) {
            float mag = std::abs(fftData[i]) / (FFT_SIZE / 2.0f); float db = 20.0f * std::log10(std::max(1e-6f, mag));
            float val = (db + 70.0f) / 70.0f; val = std::max(0.0f, std::min(1.0f, val));
            spectrumSmooth[i] = spectrumSmooth[i] * 0.8f + val * 0.2f; 
        }
        
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Synth UI", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

        // --- ГЛОБАЛЬНЫЙ БЛОК ВВЕРХУ ---
        ImGui::BeginGroup(); ImGui::Text("KEYBOARD"); ImGui::PushItemWidth(100); ImGui::DragInt("OCTAVE", &keyboardOctave, 0.1f, 1, 7); ImGui::PopItemWidth(); ImGui::EndGroup();
        ImGui::SameLine(0, 30); ImGui::BeginGroup(); ImGui::Text("MIDI IN"); ImGui::PushItemWidth(150);
        if (ImGui::BeginCombo("##MidiIn", selectedMidiPort >= 0 && selectedMidiPort < (int)midiPorts.size() ? midiPorts[selectedMidiPort].c_str() : "None")) {
            if (ImGui::Selectable("None", selectedMidiPort == -1)) { selectedMidiPort = -1; OpenMidiPort(-1); }
            for (int i = 0; i < (int)midiPorts.size(); ++i) { if (ImGui::Selectable(midiPorts[i].c_str(), selectedMidiPort == i)) { selectedMidiPort = i; OpenMidiPort(i); } }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth(); ImGui::EndGroup();
        ImGui::SameLine(0, 30); ImGui::BeginGroup(); ImGui::Text("PRESET: %s", currentPresetName.c_str()); 
        if (ImGui::Button("SAVE")) { std::string path = SaveFileDialog(); if (!path.empty()) { SavePreset(path); currentPresetName = path.substr(path.find_last_of("/\\") + 1); } } ImGui::SameLine();
        if (ImGui::Button("LOAD")) { std::string path = OpenFileDialog(); if (!path.empty()) { LoadPreset(path); currentPresetName = path.substr(path.find_last_of("/\\") + 1); } }
        ImGui::EndGroup();
        ImGui::SameLine(0, 30); ImGui::BeginGroup(); ImGui::Text("MASTER"); DrawKnob("Vol", &masterVolumeDb, -36.0f, 12.0f, "%.1f dB", TGT_NONE); ImGui::EndGroup();
        ImGui::Separator();

        // === НАВИГАЦИЯ ПО ВКЛАДКАМ ===
        if (ImGui::BeginTabBar("SerumTabs")) {
            
            // 1. ВКЛАДКА OSC
            if (ImGui::BeginTabItem("OSC")) {
                ImGui::BeginGroup(); // Left Column
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255)); ImGui::BeginChild("LeftTools", ImVec2(180, 260), true);
                        ImGui::Text("SUB OSC"); ImGui::Checkbox("##SubOn", &subEnabled); ImGui::SameLine(); ImGui::PushItemWidth(60); ImGui::DragInt("Oct", &subOctave, 0.1f, -4, 0); ImGui::PopItemWidth(); ImGui::SameLine(); DrawKnob("Lev##Sub", &subLevel, 0.0f, 1.0f, "%.2f", TGT_SUB_LEVEL);
                        ImGui::Separator();
                        ImGui::Text("NOISE"); ImGui::Checkbox("##NoiseOn", &globalNoise.enabled); ImGui::SameLine(0, 5); ImGui::PushItemWidth(80); const char* noiseTypes[] = { "White", "Pink", "Brown" }; ImGui::Combo("##NType", &globalNoise.type, noiseTypes, 3); ImGui::PopItemWidth(); ImGui::SameLine(0, 5); DrawKnob("Lev##Noise", &globalNoise.level, 0.0f, 1.0f, "%.2f", TGT_NOISE_LEVEL);
                        ImGui::Separator();
                        ImGui::Text("VOICING"); ImGui::Checkbox("Legato", &isMonoLegato); ImGui::SameLine(0, 20); DrawKnob("Glide", &glideTime, 0.0f, 1.0f, "%.2fs", TGT_NONE);
                    ImGui::EndChild(); ImGui::PopStyleColor(); 
                ImGui::EndGroup(); 
                ImGui::SameLine(0, 15);

                ImGui::BeginGroup(); // Right Column
                    ImGui::PushID("OSC_A"); ImGui::BeginGroup(); ImGui::Checkbox("OSC A", &oscAEnabled); ImGui::SameLine(); ImGui::PushItemWidth(150);
                    if (ImGui::ArrowButton("##leftWA", ImGuiDir_Left)) { currentTableIndexA--; if (currentTableIndexA < 0) currentTableIndexA = (int)wtManager.tables.size() - 1; UpdateOscillatorsTable(); } ImGui::SameLine(); ImGui::Button(wtManager.tables[currentTableIndexA].name.c_str(), ImVec2(150, 0)); ImGui::SameLine(); if (ImGui::ArrowButton("##rightWA", ImGuiDir_Right)) { currentTableIndexA++; if (currentTableIndexA >= (int)wtManager.tables.size()) currentTableIndexA = 0; UpdateOscillatorsTable(); } ImGui::PopItemWidth();
                    ImGui::SameLine(0, 20); if (DrawKnob("WT POS", &wtPosA, 0.0f, 1.0f, "%.2f", TGT_OSCA_WTPOS)) UpdateOscillatorsTable();
                    ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("PITCH"); ImGui::PushItemWidth(40); ImGui::DragInt("OCT", &oscA_Octave, 0.1f, -4, 4); ImGui::DragInt("SEM", &pitchSemiA, 0.1f, -12, 12); ImGui::PopItemWidth(); ImGui::EndGroup();
                    ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("UNISON"); ImGui::PushItemWidth(40); ImGui::DragInt("##UniA", &unisonVoicesA, 0.2f, 1, 16); ImGui::PopItemWidth(); ImGui::EndGroup();
                    ImGui::SameLine(0, 10); DrawKnob("Detune", &unisonDetuneA, 0.0f, 1.0f, "%.2f", TGT_OSCA_DETUNE); ImGui::SameLine(0, 10); DrawKnob("Blend", &unisonBlendA, 0.0f, 1.0f, "%.2f", TGT_OSCA_BLEND); ImGui::SameLine(0, 10); DrawKnob("Level", &oscALevel, 0.0f, 1.0f, "%.2f", TGT_OSCA_LEVEL);
                    ImGui::EndGroup(); ImGui::PopID(); ImGui::Separator();

                    ImGui::PushID("OSC_B"); ImGui::BeginGroup(); ImGui::Checkbox("OSC B", &oscBEnabled); ImGui::SameLine(); ImGui::PushItemWidth(150);
                    if (ImGui::ArrowButton("##leftWB", ImGuiDir_Left)) { currentTableIndexB--; if (currentTableIndexB < 0) currentTableIndexB = (int)wtManager.tables.size() - 1; UpdateOscillatorsTable(); } ImGui::SameLine(); ImGui::Button(wtManager.tables[currentTableIndexB].name.c_str(), ImVec2(150, 0)); ImGui::SameLine(); if (ImGui::ArrowButton("##rightWB", ImGuiDir_Right)) { currentTableIndexB++; if (currentTableIndexB >= (int)wtManager.tables.size()) currentTableIndexB = 0; UpdateOscillatorsTable(); } ImGui::PopItemWidth();
                    ImGui::SameLine(0, 20); if (DrawKnob("WT POS", &wtPosB, 0.0f, 1.0f, "%.2f", TGT_OSCB_WTPOS)) UpdateOscillatorsTable();
                    ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("PITCH"); ImGui::PushItemWidth(40); ImGui::DragInt("OCT", &oscB_Octave, 0.1f, -4, 4); ImGui::DragInt("SEM", &pitchSemiB, 0.1f, -12, 12); ImGui::PopItemWidth(); ImGui::EndGroup();
                    ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("UNISON"); ImGui::PushItemWidth(40); ImGui::DragInt("##UniB", &unisonVoicesB, 0.2f, 1, 16); ImGui::PopItemWidth(); ImGui::EndGroup();
                    ImGui::SameLine(0, 10); DrawKnob("Detune", &unisonDetuneB, 0.0f, 1.0f, "%.2f", TGT_OSCB_DETUNE); ImGui::SameLine(0, 10); DrawKnob("Blend", &unisonBlendB, 0.0f, 1.0f, "%.2f", TGT_OSCB_BLEND); ImGui::SameLine(0, 10); DrawKnob("Level", &oscBLevel, 0.0f, 1.0f, "%.2f", TGT_OSCB_LEVEL);
                    ImGui::EndGroup(); ImGui::PopID(); ImGui::Separator();

                    ImGui::BeginGroup(); // Envelopes
                    ImGui::BeginTabBar("EnvTabs");
                    for (int i = 0; i < 3; ++i) {
                        char envName[16]; snprintf(envName, sizeof(envName), i==0 ? "ENV 1 (AMP)" : "ENV %d", i+1);
                        if (ImGui::BeginTabItem(envName)) {
                            uiSelectedModSource = SRC_ENV1 + i;
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f)); ImGui::Button("(+) DRAG MOD", ImVec2(90, 25)); ImGui::PopStyleColor();
                            if (ImGui::BeginDragDropSource()) { int src = SRC_ENV1 + i; ImGui::SetDragDropPayload("MOD_SRC", &src, sizeof(int)); ImGui::Text("Assign ENV %d", i+1); ImGui::EndDragDropSource(); }

                            ImGui::SameLine(0, 20); bool envC = false;
                            if (DrawKnob("A", &envA[i], 0.001f, 5.0f, "%.2fs", TGT_NONE)) envC=true; ImGui::SameLine(0, 10); if (DrawKnob("D", &envD[i], 0.001f, 5.0f, "%.2fs", TGT_NONE)) envC=true; ImGui::SameLine(0, 10); if (DrawKnob("S", &envS[i], 0.0f, 1.0f, "%.2f", TGT_NONE)) envC=true; ImGui::SameLine(0, 10); if (DrawKnob("R", &envR[i], 0.001f, 5.0f, "%.2fs", TGT_NONE)) envC=true; 
                            if (envC) UpdateEnvelopes();
                            
                            ImVec2 envPos = ImGui::GetCursorScreenPos(); ImVec2 envSize(260.0f, 60.0f); ImGui::InvisibleButton("##EnvGraph", envSize); ImDrawList* ed = ImGui::GetWindowDrawList(); ed->AddRectFilled(envPos, ImVec2(envPos.x + envSize.x, envPos.y + envSize.y), IM_COL32(30, 30, 35, 255)); 
                            float tt = 10.0f; float pxA = (envA[i] / tt) * envSize.x * 2.0f; float pxD = (envD[i] / tt) * envSize.x * 2.0f; float pxS = 40.0f; float pxR = (envR[i] / tt) * envSize.x * 2.0f; 
                            ImVec2 p0(envPos.x, envPos.y + envSize.y), p1(p0.x + pxA, envPos.y), p2(p1.x + pxD, envPos.y + envSize.y - (envS[i] * envSize.y)), p3(p2.x + pxS, p2.y), p4(p3.x + pxR, envPos.y + envSize.y); 
                            ed->AddLine(p0, p1, IM_COL32(0, 200, 255, 255), 2.0f); ed->AddLine(p1, p2, IM_COL32(0, 200, 255, 255), 2.0f); ed->AddLine(p2, p3, IM_COL32(0, 200, 255, 255), 2.0f); ed->AddLine(p3, p4, IM_COL32(0, 200, 255, 255), 2.0f); ed->AddCircleFilled(p1, 4.0f, IM_COL32(255, 255, 255, 255)); ed->AddCircleFilled(p2, 4.0f, IM_COL32(255, 255, 255, 255)); ed->AddCircleFilled(p4, 4.0f, IM_COL32(255, 255, 255, 255));
                            
                            float lvl = 0; int state = 0;
                            if (i == 0) { float maxLvl = -1.0f; for (int v = 0; v < MAX_VOICES; ++v) { if (voicesA[v].isActive() && voicesA[v].env.currentLevel > maxLvl) { maxLvl = voicesA[v].env.currentLevel; lvl = maxLvl; state = voicesA[v].env.state; } } }
                            else { lvl = auxEnvs[i-1].val; state = auxEnvs[i-1].state; }
                            
                            if (state != 0) {
                                float dotY = envPos.y + envSize.y - (lvl * envSize.y); float dotX = envPos.x;
                                if (state == 1) dotX = p0.x + pxA * lvl; else if (state == 2) { float r = 1.0f - envS[i]; dotX = p1.x + pxD * ((r > 0.001f) ? ((1.0f - lvl)/r) : 1.0f); } else if (state == 3) dotX = p2.x + pxS * 0.5f; else if (state == 4) { float t = (envS[i] > 0.001f) ? (1.0f - (lvl / envS[i])) : 1.0f; dotX = p3.x + pxR * std::max(0.0f, std::min(1.0f, t)); }
                                ed->AddCircleFilled(ImVec2(dotX, dotY), 4.5f, IM_COL32(255, 215, 0, 255));
                            }
                            ImGui::EndTabItem();
                        }
                    }
                    ImGui::EndTabBar();
                    ImGui::EndGroup(); 
                    
                    ImGui::SameLine(0, 40); ImGui::PushID("FILTER1"); ImGui::BeginGroup(); ImGui::Text("FILTER"); ImGui::BeginGroup(); bool filterChanged = false;
                    if (ImGui::Checkbox("##FiltEnable", &filterEnabled)) filterChanged = true; ImGui::SameLine(); ImGui::PushItemWidth(150); const char* filterTypes[] = { "Low Pass 12dB", "High Pass 12dB", "Band Pass 12dB" };
                    if (ImGui::Combo("##FiltType", &filterType, filterTypes, 3)) filterChanged = true; ImGui::PopItemWidth(); ImGui::SameLine(0, 20); if (DrawKnob("Cutoff", &filterCutoff, 20.0f, 20000.0f, "%.0f Hz", TGT_FILT_CUTOFF, true)) filterChanged = true; ImGui::SameLine(0, 10); if (DrawKnob("Res", &filterResonance, 0.1f, 10.0f, "%.2f Q", TGT_FILT_RES)) filterChanged = true; if (filterChanged) UpdateFilters(); ImGui::EndGroup(); 
                    ImGui::SameLine(0, 20); ImVec2 filtPos = ImGui::GetCursorScreenPos(); ImVec2 filtSize(250.0f, 80.0f); ImGui::InvisibleButton("##FiltGraph", filtSize); ImDrawList* fd = ImGui::GetWindowDrawList(); fd->AddRectFilled(filtPos, ImVec2(filtPos.x + filtSize.x, filtPos.y + filtSize.y), IM_COL32(30, 30, 35, 255));
                    if (filterEnabled) {
                        std::vector<ImVec2> pts; int numPoints = (int)filtSize.x;
                        for (int i = 0; i < numPoints; ++i) { float freq = 20.0f * std::pow(1000.0f, (float)i / (numPoints - 1.0f)); float magDb = 20.0f * std::log10(std::max(0.0001f, voicesA[0].filter.getMagnitude(freq, 44100.0f))); pts.push_back(ImVec2(filtPos.x + i, filtPos.y + std::max(0.0f, std::min(1.0f, 1.0f - (magDb + 24.0f) / 42.0f)) * filtSize.y)); }
                        for (size_t i = 0; i < pts.size() - 1; ++i) fd->AddLine(pts[i], pts[i+1], IM_COL32(0, 255, 100, 255), 2.0f);
                        float liveCutoff = std::max(20.0f, std::min(20000.0f, filterCutoff * std::pow(2.0f, GetModSum(TGT_FILT_CUTOFF) * 5.0f)));
                        float xCut = filtPos.x + (std::log(liveCutoff / 20.0f) / std::log(1000.0f)) * filtSize.x;
                        if (xCut >= filtPos.x && xCut <= filtPos.x + filtSize.x) { fd->AddLine(ImVec2(xCut, filtPos.y), ImVec2(xCut, filtPos.y + filtSize.y), IM_COL32(255, 255, 255, 50), 1.0f); float yNorm = 1.0f - (20.0f * std::log10(std::max(0.0001f, voicesA[0].filter.getMagnitude(liveCutoff, 44100.0f))) + 24.0f) / 42.0f; fd->AddCircleFilled(ImVec2(xCut, filtPos.y + std::max(0.0f, std::min(1.0f, yNorm)) * filtSize.y), 4.0f, IM_COL32(255, 255, 255, 255)); }
                    } else { fd->AddLine(ImVec2(filtPos.x, filtPos.y + (1.0f - 24.0f / 42.0f) * filtSize.y), ImVec2(filtPos.x + filtSize.x, filtPos.y + (1.0f - 24.0f / 42.0f) * filtSize.y), IM_COL32(100, 100, 100, 255), 2.0f); }
                    ImGui::EndGroup(); ImGui::PopID(); ImGui::Separator();

                    ImGui::BeginGroup(); // LFOs
                    ImGui::BeginTabBar("LfoTabs");
                    for (int i = 0; i < 2; ++i) {
                        char lfoName[16]; snprintf(lfoName, sizeof(lfoName), "LFO %d", i+1);
                        if (ImGui::BeginTabItem(lfoName)) {
                            uiSelectedModSource = SRC_LFO1 + i;
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.8f, 1.0f)); ImGui::Button("(+) DRAG MOD", ImVec2(90, 25)); ImGui::PopStyleColor();
                            if (ImGui::BeginDragDropSource()) { int src = SRC_LFO1 + i; ImGui::SetDragDropPayload("MOD_SRC", &src, sizeof(int)); ImGui::Text("Assign LFO %d", i+1); ImGui::EndDragDropSource(); }
                            ImGui::SameLine(0, 20); ImGui::PushItemWidth(80);
                            const char* lfoShapes[] = { "Sine", "Triangle", "Saw", "Square" }; ImGui::Combo("Shape", &globalLFOs[i].shape, lfoShapes, 4); ImGui::SameLine();
                            const char* syncModes[] = { "Hz", "BPM" }; ImGui::Combo("Sync", &lfoSyncMode[i], syncModes, 2);
                            if (lfoSyncMode[i] == 1) { ImGui::SameLine(); ImGui::DragFloat("BPM", &globalBpm, 1.0f, 20.0f, 300.0f, "%.1f"); ImGui::SameLine(); const char* rates[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }; ImGui::Combo("Rate", &lfoRateBPMIndex[i], rates, 6); } 
                            else { ImGui::SameLine(); ImGui::SliderFloat("Rate", &lfoRateHz[i], 0.05f, 20.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic); }
                            ImGui::SameLine(); ImGui::ProgressBar((globalLFOs[i].currentValue + 1.0f) / 2.0f, ImVec2(100, 20), "Phase"); ImGui::PopItemWidth(); ImGui::EndTabItem();
                        }
                    }
                    ImGui::EndTabBar(); ImGui::EndGroup(); 

                    ImGui::SameLine(0, 30); ImGui::BeginGroup(); ImGui::Text("EQ SPECTRUM"); ImVec2 specPos = ImGui::GetCursorScreenPos(); float specW = ImGui::GetContentRegionAvail().x - 10.0f; float specH = 80.0f;
                    ImGui::InvisibleButton("##Spectrum", ImVec2(specW, specH)); ImDrawList* specDraw = ImGui::GetWindowDrawList(); specDraw->AddRectFilled(specPos, ImVec2(specPos.x + specW, specPos.y + specH), IM_COL32(20, 20, 25, 255));
                    for(int x = 0; x < (int)specW - 1; x += 3) {
                        float t = (float)x / specW; float freq = 20.0f * std::pow(1000.0f, t); float binF = freq * (FFT_SIZE / 44100.0f); int bin = std::max(0, std::min((int)binF, FFT_SIZE/2 - 1)); float val = spectrumSmooth[bin];
                        specDraw->AddLine(ImVec2(specPos.x + x, specPos.y + specH), ImVec2(specPos.x + x, specPos.y + specH - val * specH), IM_COL32(255, 100, 200, 200), 2.0f); 
                    }
                    ImGui::EndGroup(); ImGui::Separator();

                    ImVec2 pos3d=ImGui::GetCursorScreenPos(); float totalW=ImGui::GetContentRegionAvail().x; float viewH3d=160.0f; ImGui::InvisibleButton("##3DViewSplit", ImVec2(totalW, viewH3d)); ImDrawList* drawList = ImGui::GetWindowDrawList(); 
                    Draw3DTable(drawList, pos3d, totalW/2.0f - 5.0f, viewH3d, wtManager.tables[currentTableIndexA], std::max(0.0f, std::min(1.0f, wtPosA + GetModSum(TGT_OSCA_WTPOS))), std::max(0.0f, std::min(1.0f, oscALevel + GetModSum(TGT_OSCA_LEVEL))), voicesA[0].getWavetableData(), oscAEnabled);
                    Draw3DTable(drawList, ImVec2(pos3d.x + totalW/2.0f + 5.0f, pos3d.y), totalW/2.0f - 5.0f, viewH3d, wtManager.tables[currentTableIndexB], std::max(0.0f, std::min(1.0f, wtPosB + GetModSum(TGT_OSCB_WTPOS))), std::max(0.0f, std::min(1.0f, oscBLevel + GetModSum(TGT_OSCB_LEVEL))), voicesB[0].getWavetableData(), oscBEnabled);

                ImGui::EndGroup(); // End Right Column
                ImGui::EndTabItem();
            }

            // 2. ВКЛАДКА DRAW (РЕДАКТОР ВОЛН)
            if (ImGui::BeginTabItem("DRAW")) {
                static std::vector<float> customWave(TABLE_SIZE, 0.0f);
                static float lastMouseX = -1.0f; static float lastMouseY = -1.0f;

                ImGui::BeginGroup();
                if (ImGui::Button("CLEAR", ImVec2(100, 30))) { std::fill(customWave.begin(), customWave.end(), 0.0f); }
                ImGui::SameLine();
                if (ImGui::Button("SMOOTH BRUSH", ImVec2(120, 30))) {
                    std::vector<float> temp = customWave;
                    for(int i=1; i<TABLE_SIZE-1; ++i) customWave[i] = (temp[i-1] + temp[i] + temp[i+1]) / 3.0f;
                }
                ImGui::SameLine(0, 50);
                if (ImGui::Button("APPLY TO OSC A (Current Frame)", ImVec2(250, 30))) {
                    std::lock_guard<std::mutex> lock(audioMutex);
                    int frameIdx = (int)(wtPosA * (wtManager.tables[currentTableIndexA].frames.size() - 1));
                    wtManager.tables[currentTableIndexA].frames[frameIdx] = customWave;
                    UpdateOscillatorsTable();
                }
                ImGui::EndGroup(); ImGui::Dummy(ImVec2(0, 10));

                ImVec2 canvasPos = ImGui::GetCursorScreenPos(); ImVec2 canvasSize = ImGui::GetContentRegionAvail(); canvasSize.y -= 10.0f; 
                ImGui::InvisibleButton("##canvas", canvasSize); ImDrawList* drawList = ImGui::GetWindowDrawList();

                drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 25, 30, 255));
                drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(100, 100, 100, 255));
                float midY = canvasPos.y + canvasSize.y * 0.5f; drawList->AddLine(ImVec2(canvasPos.x, midY), ImVec2(canvasPos.x + canvasSize.x, midY), IM_COL32(80, 80, 80, 255));

                if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
                    ImVec2 mousePos = ImGui::GetMousePos(); float x = mousePos.x - canvasPos.x; float y = mousePos.y - canvasPos.y;
                    int idx = (int)((x / canvasSize.x) * (TABLE_SIZE - 1)); float val = 1.0f - 2.0f * (y / canvasSize.y); 
                    idx = std::max(0, std::min(TABLE_SIZE - 1, idx)); val = std::max(-1.0f, std::min(1.0f, val));

                    if (lastMouseX >= 0.0f) {
                        int lastIdx = (int)((lastMouseX / canvasSize.x) * (TABLE_SIZE - 1)); lastIdx = std::max(0, std::min(TABLE_SIZE - 1, lastIdx));
                        float lastVal = 1.0f - 2.0f * (lastMouseY / canvasSize.y); lastVal = std::max(-1.0f, std::min(1.0f, lastVal));
                        int startIdx = std::min(lastIdx, idx); int endIdx = std::max(lastIdx, idx);
                        for (int i = startIdx; i <= endIdx; ++i) {
                            if (endIdx == startIdx) customWave[i] = val;
                            else { float t = (float)(i - lastIdx) / (float)(idx - lastIdx); customWave[i] = lastVal + t * (val - lastVal); }
                        }
                    } else { customWave[idx] = val; }
                    lastMouseX = x; lastMouseY = y;
                } else { lastMouseX = -1.0f; lastMouseY = -1.0f; }

                for (int i = 0; i < TABLE_SIZE - 1; ++i) {
                    float x1 = canvasPos.x + ((float)i / (TABLE_SIZE - 1)) * canvasSize.x; float x2 = canvasPos.x + ((float)(i + 1) / (TABLE_SIZE - 1)) * canvasSize.x;
                    float y1 = midY - customWave[i] * (canvasSize.y * 0.5f); float y2 = midY - customWave[i + 1] * (canvasSize.y * 0.5f);
                    drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(255, 150, 0, 255), 2.0f);
                }
                ImGui::EndTabItem();
            }

            // 3. ВКЛАДКА FX
            if (ImGui::BeginTabItem("FX")) {
                ImGui::Dummy(ImVec2(0.0f, 10.0f)); ImGui::PushID("FX_DIST"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255)); ImGui::BeginChild("DistortionPanel", ImVec2(0, 100), true); ImGui::Checkbox("DISTORTION", &globalDistortion.enabled); ImGui::SameLine(150); DrawKnob("Drive", &globalDistortion.drive, 0.0f, 1.0f, "%.2f", TGT_DIST_DRIVE); ImGui::SameLine(250); DrawKnob("Mix", &globalDistortion.mix, 0.0f, 1.0f, "%.2f", TGT_DIST_MIX); ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();
                ImGui::Dummy(ImVec2(0.0f, 10.0f)); ImGui::PushID("FX_DELAY"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255)); ImGui::BeginChild("DelayPanel", ImVec2(0, 100), true); ImGui::Checkbox("DELAY", &globalDelay.enabled); ImGui::SameLine(150); DrawKnob("Time", &globalDelay.time, 0.01f, 1.5f, "%.2fs", TGT_DEL_TIME); ImGui::SameLine(250); DrawKnob("Fback", &globalDelay.feedback, 0.0f, 0.95f, "%.2f", TGT_DEL_FB); ImGui::SameLine(350); DrawKnob("Mix", &globalDelay.mix, 0.0f, 1.0f, "%.2f", TGT_DEL_MIX); ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();
                ImGui::Dummy(ImVec2(0.0f, 10.0f)); ImGui::PushID("FX_REV"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255)); ImGui::BeginChild("ReverbPanel", ImVec2(0, 100), true); ImGui::Checkbox("REVERB", &globalReverb.enabled); ImGui::SameLine(150); DrawKnob("Size", &globalReverb.roomSize, 0.0f, 0.98f, "%.2f", TGT_REV_SIZE); ImGui::SameLine(250); DrawKnob("Damp", &globalReverb.damping, 0.0f, 1.0f, "%.2f", TGT_REV_DAMP); ImGui::SameLine(350); DrawKnob("Mix", &globalReverb.mix, 0.0f, 1.0f, "%.2f", TGT_REV_MIX); ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();
                ImGui::Dummy(ImVec2(0.0f, 60.0f)); ImGui::EndTabItem();
            }

            // 4. ВКЛАДКА MOD MATRIX
            if (ImGui::BeginTabItem("MOD MATRIX")) {
                ImGui::Columns(4, "modmatrix_cols"); ImGui::Separator();
                ImGui::Text("SOURCE"); ImGui::NextColumn(); ImGui::Text("AMOUNT"); ImGui::NextColumn(); ImGui::Text("DESTINATION"); ImGui::NextColumn(); ImGui::Text("ACTION"); ImGui::NextColumn(); ImGui::Separator();
                const char* srcNames[] = { "None", "LFO 1", "LFO 2", "ENV 1", "ENV 2", "ENV 3" };
                const char* tgtNames[] = { "None", "Osc A WT Pos", "Osc A Pitch", "Osc A Level", "Osc A Detune", "Osc A Blend", "Osc B WT Pos", "Osc B Pitch", "Osc B Level", "Osc B Detune", "Osc B Blend", "Filter Cutoff", "Filter Res", "Sub Level", "Noise Level", "Dist Drive", "Dist Mix", "Delay Time", "Delay FB", "Delay Mix", "Reverb Size", "Reverb Damp", "Reverb Mix" };
                for (int i = 0; i < (int)modMatrix.size(); ++i) { ImGui::PushID(i); ImGui::Text("%s", srcNames[modMatrix[i].source]); ImGui::NextColumn(); ImGui::SliderFloat("##amt", &modMatrix[i].amount, -1.0f, 1.0f, "%.2f"); ImGui::NextColumn(); ImGui::Text("%s", tgtNames[modMatrix[i].target]); ImGui::NextColumn(); if (ImGui::Button("REMOVE", ImVec2(80, 0))) { modMatrix.erase(modMatrix.begin() + i); i--; } ImGui::NextColumn(); ImGui::PopID(); }
                ImGui::Columns(1); ImGui::Separator(); ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        ImGui::Separator();

        // === ОСЦИЛЛОГРАФ И ПИАНИНО ===
        ImVec2 scopePos = ImGui::GetCursorScreenPos(); float scopeW = ImGui::GetContentRegionAvail().x; float scopeH = 60.0f; ImGui::InvisibleButton("##MainScope", ImVec2(scopeW, scopeH)); ImDrawList* scopeDraw = ImGui::GetWindowDrawList();
        scopeDraw->AddRectFilled(scopePos, ImVec2(scopePos.x + scopeW, scopePos.y + scopeH), IM_COL32(15, 15, 20, 255)); float midY = scopePos.y + scopeH * 0.5f; scopeDraw->AddLine(ImVec2(scopePos.x, midY), ImVec2(scopePos.x + scopeW, midY), IM_COL32(40, 40, 50, 255), 1.0f); 
        for (int i = 0; i < SCOPE_SIZE - 1; ++i) {
            float x1 = scopePos.x + ((float)i / (SCOPE_SIZE - 1)) * scopeW; float x2 = scopePos.x + ((float)(i + 1) / (SCOPE_SIZE - 1)) * scopeW;
            float y1 = midY - std::max(-1.0f, std::min(1.0f, scopeBuffer[i])) * (scopeH * 0.45f); float y2 = midY - std::max(-1.0f, std::min(1.0f, scopeBuffer[i+1])) * (scopeH * 0.45f);
            scopeDraw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 255, 150, 255), 1.5f);
        }
        ImGui::Separator();

        ImGui::BeginChild("PianoScroll",ImVec2(0,200),true,ImGuiWindowFlags_HorizontalScrollbar);float whiteW=36.0f,whiteH=150.0f,blackW=22.0f,blackH=90.0f,spacing=1.0f;int startOct=2,endOct=7,currentMouseNote=-1;ImVec2 startPosP=ImGui::GetCursorPos();int whiteIndex=0;for(int oct=startOct;oct<=endOct;++oct){for(int note=0;note<12;++note){if(note==1||note==3||note==6||note==8||note==10)continue;int midi=(oct+1)*12+note;ImGui::SetCursorPos(ImVec2(startPosP.x+whiteIndex*(whiteW+spacing),startPosP.y));ImGui::PushID(midi);if(PianoKey("##w",false,noteState[midi],ImVec2(whiteW,whiteH)))currentMouseNote=midi;ImGui::PopID();if(note==0){ImGui::SetCursorPos(ImVec2(startPosP.x+whiteIndex*(whiteW+spacing)+8,startPosP.y+whiteH-20));ImGui::TextColored(ImVec4(0.2f,0.2f,0.2f,1.0f),"C%d",oct);}whiteIndex++;}}whiteIndex=0;for(int oct=startOct;oct<=endOct;++oct){for(int note=0;note<12;++note){bool isBlack=(note==1||note==3||note==6||note==8||note==10);if(!isBlack){whiteIndex++;continue;}int midi=(oct+1)*12+note;float offsetX=startPosP.x+whiteIndex*(whiteW+spacing)-(blackW/2.0f)-(spacing/2.0f);ImGui::SetCursorPos(ImVec2(offsetX,startPosP.y));ImGui::PushID(midi);if(PianoKey("##b",true,noteState[midi],ImVec2(blackW,blackH)))currentMouseNote=midi;ImGui::PopID();}}ImGui::SetCursorPos(ImVec2(startPosP.x,startPosP.y+whiteH+10));ImGui::EndChild();if(mouseHeldNote!=-1&&mouseHeldNote!=currentMouseNote){if(!keysPressedMidi[mouseHeldNote])NoteOff(mouseHeldNote);mouseHeldNote=-1;}if(currentMouseNote!=-1&&currentMouseNote!=mouseHeldNote){NoteOn(currentMouseNote);mouseHeldNote=currentMouseNote;}

        ImGui::End(); ImGui::Render(); int w, h; glfwGetFramebufferSize(window, &w, &h); glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); glfwSwapBuffers(window);
    }
    
    audioDriver.stop(); if (hMidiIn) { midiInStop(hMidiIn); midiInClose(hMidiIn); } ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate(); return 0;
}