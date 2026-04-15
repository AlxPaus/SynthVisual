#include "imgui.h"
#include "synth/synth_globals.h"
#include "ui/ui_widgets.h"
#include "ui/fx_tab.h"

static void RenderFxPanel(const char* id, const char* title, bool& enabled) {
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255));
    ImGui::BeginChild(id, ImVec2(0, 100), true);
    ImGui::Checkbox(title, &enabled);
}

static void EndFxPanel() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
}

void RenderFxTab() {
    RenderFxPanel("FX_DIST", "DISTORTION", g_synth.distortion.enabled);
    ImGui::SameLine(150); DrawKnob("Drive", &g_synth.distortion.drive, 0.0f, 1.0f, "%.2f", TGT_DIST_DRIVE);
    ImGui::SameLine(250); DrawKnob("Mix",   &g_synth.distortion.mix,   0.0f, 1.0f, "%.2f", TGT_DIST_MIX);
    EndFxPanel();

    RenderFxPanel("FX_DELAY", "DELAY", g_synth.delay.enabled);
    ImGui::SameLine(150); DrawKnob("Time",  &g_synth.delay.time,     0.01f, 1.5f,  "%.2fs", TGT_DEL_TIME);
    ImGui::SameLine(250); DrawKnob("Fback", &g_synth.delay.feedback, 0.0f,  0.95f, "%.2f",  TGT_DEL_FB);
    ImGui::SameLine(350); DrawKnob("Mix",   &g_synth.delay.mix,      0.0f,  1.0f,  "%.2f",  TGT_DEL_MIX);
    EndFxPanel();

    RenderFxPanel("FX_REV", "REVERB", g_synth.reverb.enabled);
    ImGui::SameLine(150); DrawKnob("Size", &g_synth.reverb.roomSize, 0.0f, 0.98f, "%.2f", TGT_REV_SIZE);
    ImGui::SameLine(250); DrawKnob("Damp", &g_synth.reverb.damping,  0.0f, 1.0f,  "%.2f", TGT_REV_DAMP);
    ImGui::SameLine(350); DrawKnob("Mix",  &g_synth.reverb.mix,      0.0f, 1.0f,  "%.2f", TGT_REV_MIX);
    EndFxPanel();

    ImGui::Dummy(ImVec2(0.0f, 60.0f));
}
