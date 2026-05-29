#include <mutex>
#include <cstdio>
#include "imgui.h"
#include "synth/synth_globals.h"
#include "synth/audio_engine.h"
#include "synth/presets.h"
#include "synth/wav_export.h"
#include "ui/ui_widgets.h"
#include "ui/top_bar.h"

void RenderTopBar(std::string& presetName) {
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
    bool isRec    = g_synth.isRecording.load(std::memory_order_relaxed);
    bool isPaused = g_synth.isRecordingPaused.load(std::memory_order_relaxed);

    if (isRec) {
        float recSec  = (float)(g_synth.recordBuffer.size() / 2) / 44100.0f;
        int   minutes = (int)recSec / 60;
        int   secs    = (int)recSec % 60;
        int   cs      = (int)(recSec * 100) % 100;
        char  timeBuf[24];
        snprintf(timeBuf, sizeof(timeBuf), isPaused ? "[P] %02d:%02d.%02d" : "REC %02d:%02d.%02d",
                 minutes, secs, cs);
        ImGui::TextColored(
            isPaused ? ImVec4(0.9f, 0.6f, 0.0f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
            "%s", timeBuf);

        if (isPaused) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            if (ImGui::Button("> RESUME"))
                g_synth.isRecordingPaused.store(false, std::memory_order_relaxed);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.0f, 1.0f));
            if (ImGui::Button("|| PAUSE"))
                g_synth.isRecordingPaused.store(true, std::memory_order_relaxed);
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("■ STOP")) {
            g_synth.isRecording.store(false, std::memory_order_seq_cst);
            g_synth.isRecordingPaused.store(false, std::memory_order_relaxed);
            std::string path = SaveWavDialog();
            if (!path.empty()) {
                std::lock_guard<std::mutex> lock(audioMutex);
                WriteWav(path, g_synth.recordBuffer, 44100, 2);
            }
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("● REC")) {
            g_synth.recordBuffer.clear();
            g_synth.isRecording.store(true, std::memory_order_relaxed);
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 30);
    ImGui::BeginGroup();
    ImGui::Text("MASTER");
    DrawKnob("Vol", &g_synth.masterVolumeDb, -36.0f, 12.0f, "%.1f dB", TGT_NONE);
    ImGui::EndGroup();
}
