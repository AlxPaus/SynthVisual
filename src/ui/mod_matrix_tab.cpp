#include <algorithm>
#include <mutex>
#include <vector>
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

    std::vector<ModRouting> modSnapshot;
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        modSnapshot = g_synth.modMatrix;
    }

    for (int i = 0; i < (int)modSnapshot.size(); ++i) {
        ImGui::PushID(i);
        int srcIdx = std::max(0, std::min((int)(sizeof(srcNames) / sizeof(srcNames[0])) - 1, modSnapshot[i].source));
        int tgtIdx = std::max(0, std::min((int)(sizeof(tgtNames) / sizeof(tgtNames[0])) - 1, modSnapshot[i].target));
        ImGui::Text("%s", srcNames[srcIdx]);
        ImGui::NextColumn();
        float amount = modSnapshot[i].amount;
        if (ImGui::SliderFloat("##amt", &amount, -1.0f, 1.0f, "%.2f")) {
            std::lock_guard<std::mutex> lock(audioMutex);
            if (i < (int)g_synth.modMatrix.size()) g_synth.modMatrix[i].amount = amount;
        }
        ImGui::NextColumn();
        ImGui::Text("%s", tgtNames[tgtIdx]);
        ImGui::NextColumn();
        if (ImGui::Button("REMOVE", ImVec2(80, 0))) {
            std::lock_guard<std::mutex> lock(audioMutex);
            if (i < (int)g_synth.modMatrix.size()) g_synth.modMatrix.erase(g_synth.modMatrix.begin() + i);
            --i;
        }
        ImGui::NextColumn();
        ImGui::PopID();
    }

    ImGui::Columns(1);
    ImGui::Separator();
}
