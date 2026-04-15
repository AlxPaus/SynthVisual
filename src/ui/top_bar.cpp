#include "imgui.h"
#include "synth/synth_globals.h"
#include "synth/audio_engine.h"
#include "synth/presets.h"
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
    ImGui::Text("MASTER");
    DrawKnob("Vol", &g_synth.masterVolumeDb, -36.0f, 12.0f, "%.1f dB", TGT_NONE);
    ImGui::EndGroup();
}
