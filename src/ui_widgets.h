#pragma once
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "synth_globals.h"
#include "audio_engine.h"

inline bool DrawKnob(const char* label, float* p_value, float v_min, float v_max, const char* format, int target_id, bool logScale = false) {
    ImGuiIO& io = ImGui::GetIO(); float radius = 20.0f; ImVec2 pos = ImGui::GetCursorScreenPos(); ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);
    ImGui::InvisibleButton(label, ImVec2(radius*2, radius*2 + 15)); bool value_changed = false;
    if (ImGui::IsItemActive() && io.MouseDelta.y != 0.0f) { float step = (v_max - v_min) * 0.005f; *p_value -= io.MouseDelta.y * step; *p_value = std::max(v_min, std::min(v_max, *p_value)); value_changed = true; }
    
    if (target_id != TGT_NONE && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MOD_SRC")) {
            int src = *(const int*)payload->Data;
            bool found = false; for (auto& m : modMatrix) { if (m.source == src && m.target == target_id) { m.amount = 0.5f; found = true; break; } }
            if (!found) modMatrix.push_back({src, target_id, 0.5f});
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList(); float angle_min = 3.141592f * 0.75f; float angle_max = 3.141592f * 2.25f;
    float t = logScale ? (std::log(*p_value/v_min) / std::log(v_max/v_min)) : ((*p_value - v_min) / (v_max - v_min)); float angle = angle_min + (angle_max - angle_min) * t;
    draw_list->AddCircleFilled(center, radius, IM_COL32(40,40,45,255), 32); draw_list->AddCircle(center, radius, IM_COL32(15,15,15,255), 32, 2.0f);
    draw_list->PathArcTo(center, radius - 2.0f, angle_min, angle, 32); draw_list->PathStroke(IM_COL32(0,200,255,255), false, 4.0f);
    
    float mod_val = GetModAmountForUI(target_id);
    if (mod_val != 0.0f) {
        float mod_t = std::max(0.0f, std::min(1.0f, t + mod_val)); float angle_mod = angle_min + (angle_max - angle_min) * mod_t;
        if (mod_val > 0) draw_list->PathArcTo(center, radius + 4.0f, angle, angle_mod, 16); else draw_list->PathArcTo(center, radius + 4.0f, angle_mod, angle, 16); draw_list->PathStroke(IM_COL32(255,150,0,255), false, 2.0f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) { char buf[32]; snprintf(buf, sizeof(buf), format, *p_value); ImVec2 tSize = ImGui::CalcTextSize(buf); draw_list->AddText(ImVec2(center.x - tSize.x/2, pos.y + radius*2 + 2), IM_COL32(255,255,255,255), buf);
    } else { ImVec2 tSize = ImGui::CalcTextSize(label); draw_list->AddText(ImVec2(center.x - tSize.x/2, pos.y + radius*2 + 2), IM_COL32(150,150,150,255), label); }
    return value_changed;
}

struct KeyMap { int key; int offset; };
inline std::vector<KeyMap> pcKeys = { {GLFW_KEY_Z, 0}, {GLFW_KEY_S, 1}, {GLFW_KEY_X, 2}, {GLFW_KEY_D, 3}, {GLFW_KEY_C, 4}, {GLFW_KEY_V, 5}, {GLFW_KEY_G, 6}, {GLFW_KEY_B, 7}, {GLFW_KEY_H, 8}, {GLFW_KEY_N, 9}, {GLFW_KEY_J, 10}, {GLFW_KEY_M, 11}, {GLFW_KEY_COMMA, 12}, {GLFW_KEY_L, 13}, {GLFW_KEY_PERIOD, 14}, {GLFW_KEY_SEMICOLON, 15}, {GLFW_KEY_SLASH, 16}, {GLFW_KEY_Q, 12}, {GLFW_KEY_2, 13}, {GLFW_KEY_W, 14}, {GLFW_KEY_3, 15}, {GLFW_KEY_E, 16}, {GLFW_KEY_R, 17}, {GLFW_KEY_5, 18}, {GLFW_KEY_T, 19}, {GLFW_KEY_6, 20}, {GLFW_KEY_Y, 21}, {GLFW_KEY_7, 22}, {GLFW_KEY_U, 23}, {GLFW_KEY_I, 24}, {GLFW_KEY_9, 25}, {GLFW_KEY_O, 26}, {GLFW_KEY_0, 27}, {GLFW_KEY_P, 28} };

inline bool PianoKey(const char* id, bool isBlack, bool isActive, ImVec2 size) {
    ImVec4 colorNorm = isBlack ? ImVec4(0.12f, 0.12f, 0.12f, 1.0f) : ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    ImVec4 colorActive = ImVec4(0.0f, 0.8f, 0.8f, 1.0f); ImGui::PushStyleColor(ImGuiCol_Button, isActive ? colorActive : colorNorm); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isActive ? colorActive : (isBlack ? ImVec4(0.3f, 0.3f, 0.3f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f))); ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorActive); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); ImGui::Button(id, size); bool isHoveredAndActive = ImGui::IsItemActive(); ImGui::PopStyleColor(4); ImGui::PopStyleVar(); return isHoveredAndActive;
}

inline void Draw3DTable(ImDrawList* drawList, ImVec2 pos, float viewW, float viewH, const Wavetable3D& table, float currentWtPos, float currentLevel, const std::vector<float>& activeWave, bool enabled) {
    drawList->AddRectFilled(pos, ImVec2(pos.x+viewW, pos.y+viewH), IM_COL32(20,20,25,255)); 
    if (!enabled) { drawList->AddText(ImVec2(pos.x + 10, pos.y + 10), IM_COL32(100,100,100,255), "OFF"); return; }
    int numFrames = (int)table.frames.size(); float plotW = viewW * 0.75f; float plotH = 40.0f; float startX = pos.x + (viewW - plotW) / 2.0f; float startY = pos.y + viewH - 50.0f; float zTiltX = 6.0f; float zTiltY = 6.0f; int step = 16; float activeFramePrecise = currentWtPos * (numFrames - 1); 
    for (int f = numFrames - 1; f >= 0; --f) {
        float zOffsetX = f * zTiltX; float zOffsetY = f * zTiltY; bool isActive = (std::abs(activeFramePrecise - f) <= 0.5f); ImU32 lineColor = isActive ? IM_COL32(0,255,255,255) : IM_COL32(80,100,150,200);
        std::vector<ImVec2> points;
        for (int i = 0; i < TABLE_SIZE; i += step) points.push_back(ImVec2(startX + ((float)i / TABLE_SIZE) * plotW + zOffsetX, startY - (table.frames[f][i] * currentLevel * plotH) - zOffsetY));
        for (size_t i = 0; i < points.size() - 1; ++i) drawList->AddQuadFilled(points[i], points[i+1], ImVec2(points[i+1].x, startY - zOffsetY + plotH), ImVec2(points[i].x, startY - zOffsetY + plotH), IM_COL32(30,35,45,255));
        for (size_t i = 0; i < points.size() - 1; ++i) drawList->AddLine(points[i], points[i+1], lineColor, isActive ? 2.0f : 1.0f);
    }
    float finalZOffsetX = activeFramePrecise * zTiltX; float finalZOffsetY = activeFramePrecise * zTiltY;
    for (int i = 0; i < TABLE_SIZE - step; i += step) {
        ImVec2 p1(startX + ((float)i / TABLE_SIZE) * plotW + finalZOffsetX, startY - (activeWave[i] * currentLevel * plotH) - finalZOffsetY);
        ImVec2 p2(startX + ((float)(i+step) / TABLE_SIZE) * plotW + finalZOffsetX, startY - (activeWave[i+step] * currentLevel * plotH) - finalZOffsetY);
        drawList->AddLine(p1, p2, IM_COL32(255,215,0,255), 2.5f);
    }
}