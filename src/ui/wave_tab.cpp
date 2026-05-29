#include <algorithm>
#include <vector>
#include <mutex>
#include "imgui.h"
#include "synth/synth_globals.h"
#include "ui/wave_tab.h"

void RenderWaveTab() {
    static float zoomX = 1.0f;   // horizontal: 1..16 (samples shown = SCOPE_SIZE / zoomX)
    static float zoomY = 1.0f;   // vertical amplitude scale: 0.25..8

    // Controls
    ImGui::PushItemWidth(200);
    ImGui::SliderFloat("Zoom X", &zoomX, 1.0f, 16.0f, "%.2fx");
    ImGui::SameLine(0, 20);
    ImGui::SliderFloat("Zoom Y", &zoomY, 0.25f, 8.0f, "%.2fx");
    ImGui::PopItemWidth();
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Reset")) { zoomX = 1.0f; zoomY = 1.0f; }

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float  w   = ImGui::GetContentRegionAvail().x;
    float  h   = ImGui::GetContentRegionAvail().y;
    ImGui::InvisibleButton("##WaveTab", ImVec2(w, h));

    // Mouse wheel over plot: wheel = Zoom Y, Shift+wheel = Zoom X
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyShift)
                zoomX = std::max(1.0f,  std::min(16.0f, zoomX * (1.0f + wheel * 0.1f)));
            else
                zoomY = std::max(0.25f, std::min(8.0f,  zoomY * (1.0f + wheel * 0.1f)));
        }
    }

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

    // Horizontal zoom: show a subset of samples stretched across full width.
    int visible = std::max(2, (int)(SCOPE_SIZE / zoomX));
    float halfH = h * 0.45f * zoomY;

    // Clip waveform to plot rect so vertical zoom doesn't overdraw controls.
    dl->PushClipRect(pos, ImVec2(pos.x + w, pos.y + h), true);
    for (int i = 0; i < visible - 1; ++i) {
        float x1 = pos.x + ((float)i       / (visible - 1)) * w;
        float x2 = pos.x + ((float)(i + 1) / (visible - 1)) * w;
        float y1 = midY - std::max(-1.0f, std::min(1.0f, scopeSnapshot[i]))     * halfH;
        float y2 = midY - std::max(-1.0f, std::min(1.0f, scopeSnapshot[i + 1])) * halfH;
        // glow: thick semi-transparent underline
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 200, 100, 60), 6.0f);
        // bright thin line on top
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 255, 150, 255), 2.0f);
    }
    dl->PopClipRect();
}
