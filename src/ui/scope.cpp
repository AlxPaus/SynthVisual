#include <algorithm>
#include "imgui.h"
#include "synth/synth_globals.h"
#include "ui/scope.h"

void RenderScopeSection() {
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
