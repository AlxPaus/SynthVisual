#include <vector>
#include <algorithm>
#include <mutex>
#include "imgui.h"
#include "synth/synth_globals.h"
#include "synth/audio_engine.h"
#include "ui/draw_tab.h"

void RenderDrawTab() {
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
