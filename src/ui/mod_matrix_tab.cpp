#include <mutex>
#include "imgui.h"
#include "synth/synth_globals.h"
#include "ui/mod_matrix_tab.h"

void RenderModMatrixTab() {
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
