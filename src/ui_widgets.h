#pragma once
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "synth_globals.h"
#include "audio_engine.h"

// === KNOB ===

inline bool DrawKnob(const char* label, float* value, float vMin, float vMax,
                     const char* format, int targetId, bool logScale = false)
{
    ImGuiIO& io   = ImGui::GetIO();
    float radius  = 20.0f;
    ImVec2 pos    = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);

    ImGui::InvisibleButton(label, ImVec2(radius * 2, radius * 2 + 15));
    bool changed = false;

    if (ImGui::IsItemActive() && io.MouseDelta.y != 0.0f) {
        float step = (vMax - vMin) * 0.005f;
        *value -= io.MouseDelta.y * step;
        *value  = std::max(vMin, std::min(vMax, *value));
        changed = true;
    }

    if (targetId != TGT_NONE && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MOD_SRC")) {
            int src = *(const int*)payload->Data;
            std::lock_guard<std::mutex> lock(audioMutex);
            bool found = false;
            for (auto& m : g_synth.modMatrix) {
                if (m.source == src && m.target == targetId) { m.amount = 0.5f; found = true; break; }
            }
            if (!found) g_synth.modMatrix.push_back({ src, targetId, 0.5f });
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float angleMin = 3.141592f * 0.75f;
    const float angleMax = 3.141592f * 2.25f;

    float t = logScale
        ? std::log(*value / vMin) / std::log(vMax / vMin)
        : (*value - vMin) / (vMax - vMin);
    float angle = angleMin + (angleMax - angleMin) * t;

    dl->AddCircleFilled(center, radius,       IM_COL32(40, 40, 45, 255),  32);
    dl->AddCircle(center,       radius,       IM_COL32(15, 15, 15, 255),  32, 2.0f);
    dl->PathArcTo(center, radius - 2.0f, angleMin, angle, 32);
    dl->PathStroke(IM_COL32(0, 200, 255, 255), false, 4.0f);

    float modAmt = GetModAmountForUI(targetId);
    if (modAmt != 0.0f) {
        float tMod   = std::max(0.0f, std::min(1.0f, t + modAmt));
        float angMod = angleMin + (angleMax - angleMin) * tMod;
        if (modAmt > 0) dl->PathArcTo(center, radius + 4.0f, angle,  angMod, 16);
        else            dl->PathArcTo(center, radius + 4.0f, angMod, angle,  16);
        dl->PathStroke(IM_COL32(255, 150, 0, 255), false, 2.0f);
    }

    char buf[32];
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        snprintf(buf, sizeof(buf), format, *value);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(center.x - sz.x / 2, pos.y + radius * 2 + 2), IM_COL32(255, 255, 255, 255), buf);
    } else {
        ImVec2 sz = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(center.x - sz.x / 2, pos.y + radius * 2 + 2), IM_COL32(150, 150, 150, 255), label);
    }

    return changed;
}

// === KEYBOARD MAPPING ===

struct KeyMap { int key; int offset; };

inline std::vector<KeyMap> pcKeys = {
    { GLFW_KEY_Z, 0 }, { GLFW_KEY_S, 1 }, { GLFW_KEY_X, 2 }, { GLFW_KEY_D, 3 },
    { GLFW_KEY_C, 4 }, { GLFW_KEY_V, 5 }, { GLFW_KEY_G, 6 }, { GLFW_KEY_B, 7 },
    { GLFW_KEY_H, 8 }, { GLFW_KEY_N, 9 }, { GLFW_KEY_J, 10}, { GLFW_KEY_M, 11},
    { GLFW_KEY_COMMA, 12 }, { GLFW_KEY_L, 13 }, { GLFW_KEY_PERIOD, 14 },
    { GLFW_KEY_SEMICOLON, 15 }, { GLFW_KEY_SLASH, 16 },
    { GLFW_KEY_Q, 12 }, { GLFW_KEY_2, 13 }, { GLFW_KEY_W, 14 }, { GLFW_KEY_3, 15 },
    { GLFW_KEY_E, 16 }, { GLFW_KEY_R, 17 }, { GLFW_KEY_5, 18 }, { GLFW_KEY_T, 19 },
    { GLFW_KEY_6, 20 }, { GLFW_KEY_Y, 21 }, { GLFW_KEY_7, 22 }, { GLFW_KEY_U, 23 },
    { GLFW_KEY_I, 24 }, { GLFW_KEY_9, 25 }, { GLFW_KEY_O, 26 }, { GLFW_KEY_0, 27 },
    { GLFW_KEY_P, 28 }
};

// === PIANO KEY ===

inline bool PianoKey(const char* id, bool isBlack, bool isActive, ImVec2 size) {
    ImVec4 colorNorm   = isBlack ? ImVec4(0.12f, 0.12f, 0.12f, 1.0f)
                                 : ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    ImVec4 colorActive = ImVec4(0.0f, 0.8f, 0.8f, 1.0f);
    ImVec4 colorHover  = isBlack ? ImVec4(0.3f, 0.3f, 0.3f, 1.0f)
                                 : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button,        isActive ? colorActive : colorNorm);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  isActive ? colorActive : colorHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   colorActive);
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    ImGui::Button(id, size);
    bool pressed = ImGui::IsItemActive();

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return pressed;
}

// === 3D WAVETABLE VIEW ===

inline void Draw3DTable(ImDrawList* drawList, ImVec2 pos, float viewW, float viewH,
                        const Wavetable3D& table, float currentWtPos, float currentLevel,
                        const std::vector<float>& activeWave, bool enabled)
{
    drawList->AddRectFilled(pos, ImVec2(pos.x + viewW, pos.y + viewH), IM_COL32(20, 20, 25, 255));

    if (!enabled) {
        drawList->AddText(ImVec2(pos.x + 10, pos.y + 10), IM_COL32(100, 100, 100, 255), "OFF");
        return;
    }

    int   numFrames      = (int)table.frames.size();
    float plotW          = viewW * 0.75f;
    float plotH          = 40.0f;
    float startX         = pos.x + (viewW - plotW) / 2.0f;
    float startY         = pos.y + viewH - 50.0f;
    float zTiltX         = 6.0f, zTiltY = 6.0f;
    int   step           = 16;
    float activeFrame    = currentWtPos * (numFrames - 1);

    for (int f = numFrames - 1; f >= 0; --f) {
        float zX = f * zTiltX;
        float zY = f * zTiltY;
        bool  isActiveFrame = (std::abs(activeFrame - f) <= 0.5f);
        ImU32 lineColor = isActiveFrame ? IM_COL32(0, 255, 255, 255) : IM_COL32(80, 100, 150, 200);

        std::vector<ImVec2> pts;
        for (int i = 0; i < TABLE_SIZE; i += step)
            pts.push_back(ImVec2(startX + ((float)i / TABLE_SIZE) * plotW + zX,
                                 startY - table.frames[f][i] * currentLevel * plotH - zY));

        for (size_t i = 0; i < pts.size() - 1; ++i)
            drawList->AddQuadFilled(pts[i], pts[i+1],
                ImVec2(pts[i+1].x, startY - zY + plotH),
                ImVec2(pts[i].x,   startY - zY + plotH),
                IM_COL32(30, 35, 45, 255));

        for (size_t i = 0; i < pts.size() - 1; ++i)
            drawList->AddLine(pts[i], pts[i+1], lineColor, isActiveFrame ? 2.0f : 1.0f);
    }

    float fzX = activeFrame * zTiltX;
    float fzY = activeFrame * zTiltY;
    for (int i = 0; i < TABLE_SIZE - step; i += step) {
        ImVec2 p1(startX + ((float)i        / TABLE_SIZE) * plotW + fzX,
                  startY - activeWave[i]        * currentLevel * plotH - fzY);
        ImVec2 p2(startX + ((float)(i+step) / TABLE_SIZE) * plotW + fzX,
                  startY - activeWave[i + step] * currentLevel * plotH - fzY);
        drawList->AddLine(p1, p2, IM_COL32(255, 215, 0, 255), 2.5f);
    }
}
