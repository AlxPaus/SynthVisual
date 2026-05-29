#include <algorithm>
#include <vector>
#include <mutex>
#include "imgui.h"
#include "synth/synth_globals.h"
#include "ui/wave_tab.h"

void RenderWaveTab() {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float  w   = ImGui::GetContentRegionAvail().x;
    float  h   = ImGui::GetContentRegionAvail().y;
    ImGui::InvisibleButton("##WaveTab", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(10, 12, 15, 255));

    // Grid lines
    int gridH = 4, gridV = 8;
    for (int i = 1; i < gridH; ++i) {
        float y = pos.y + h * ((float)i / gridH);
        dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + w, y), IM_COL32(30, 40, 35, 200), 1.0f);
    }
    for (int i = 1; i < gridV; ++i) {
        float x = pos.x + w * ((float)i / gridV);
        dl->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + h), IM_COL32(30, 40, 35, 200), 1.0f);
    }

    float midY = pos.y + h * 0.5f;
    dl->AddLine(ImVec2(pos.x, midY), ImVec2(pos.x + w, midY), IM_COL32(40, 60, 50, 255), 1.0f);

    std::vector<float> scopeSnapshot;
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        scopeSnapshot = g_synth.scopeBuffer;
    }
    if (scopeSnapshot.size() < SCOPE_SIZE) return;

    float halfH = h * 0.45f;
    for (int i = 0; i < SCOPE_SIZE - 1; ++i) {
        float x1 = pos.x + ((float)i       / (SCOPE_SIZE - 1)) * w;
        float x2 = pos.x + ((float)(i + 1) / (SCOPE_SIZE - 1)) * w;
        float y1 = midY - std::max(-1.0f, std::min(1.0f, scopeSnapshot[i]))     * halfH;
        float y2 = midY - std::max(-1.0f, std::min(1.0f, scopeSnapshot[i + 1])) * halfH;
        // glow: thick semi-transparent underline
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 200, 100, 60), 6.0f);
        // bright thin line on top
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 255, 150, 255), 2.0f);
    }
}
