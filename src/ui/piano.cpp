#include <algorithm>
#include "imgui.h"
#include <GLFW/glfw3.h>
#include "synth/synth_globals.h"
#include "synth/audio_engine.h"
#include "ui/ui_widgets.h"
#include "ui/piano.h"

void HandlePcKeyboard(GLFWwindow* window) {
    for (const auto& k : pcKeys) {
        bool isDown = (glfwGetKey(window, k.key) == GLFW_PRESS);
        if (isDown && g_synth.pcKeyNote[k.key] == -1) {
            int note = ((g_synth.keyboardOctave + 1) * 12) + k.offset;
            note = std::max(0, std::min(127, note));
            g_synth.pcKeyNote[k.key] = note;
            g_synth.pcNoteHeld[note] = true;
            NoteOn(note);
        } else if (!isDown && g_synth.pcKeyNote[k.key] != -1) {
            int note = g_synth.pcKeyNote[k.key];
            g_synth.pcNoteHeld[note] = false;
            g_synth.pcKeyNote[k.key] = -1;
            NoteOff(note);
        }
    }
}

void RenderPianoSection(int& mouseHeldNote) {
    const float whiteW  = 36.0f, whiteH = 150.0f;
    const float blackW  = 22.0f, blackH = 90.0f;
    const float spacing = 1.0f;
    const int   startOct = 2, endOct = 7;

    ImGui::BeginChild("PianoScroll", ImVec2(0, 200), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 origin = ImGui::GetCursorPos();
    int currentMouseNote = -1;

    int whiteIndex = 0;
    for (int oct = startOct; oct <= endOct; ++oct) {
        for (int note = 0; note < 12; ++note) {
            if (note == 1 || note == 3 || note == 6 || note == 8 || note == 10) continue;
            int midi = (oct + 1) * 12 + note;
            ImGui::SetCursorPos(ImVec2(origin.x + whiteIndex * (whiteW + spacing), origin.y));
            ImGui::PushID(midi);
            if (PianoKey("##w", false, g_synth.noteActive[midi], ImVec2(whiteW, whiteH)))
                currentMouseNote = midi;
            ImGui::PopID();
            if (note == 0) {
                ImGui::SetCursorPos(ImVec2(origin.x + whiteIndex * (whiteW + spacing) + 8, origin.y + whiteH - 20));
                ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.2f, 1.0f), "C%d", oct);
            }
            ++whiteIndex;
        }
    }

    whiteIndex = 0;
    for (int oct = startOct; oct <= endOct; ++oct) {
        for (int note = 0; note < 12; ++note) {
            bool isBlack = (note == 1 || note == 3 || note == 6 || note == 8 || note == 10);
            if (!isBlack) { ++whiteIndex; continue; }
            int   midi    = (oct + 1) * 12 + note;
            float offsetX = origin.x + whiteIndex * (whiteW + spacing) - blackW / 2.0f - spacing / 2.0f;
            ImGui::SetCursorPos(ImVec2(offsetX, origin.y));
            ImGui::PushID(midi);
            if (PianoKey("##b", true, g_synth.noteActive[midi], ImVec2(blackW, blackH)))
                currentMouseNote = midi;
            ImGui::PopID();
        }
    }

    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + whiteH + 10));
    ImGui::EndChild();

    // Release previous mouse-held note if mouse moved away
    if (mouseHeldNote != -1 && mouseHeldNote != currentMouseNote) {
        if (!g_synth.pcNoteHeld[mouseHeldNote])  // don't release if PC keyboard is still holding it
            NoteOff(mouseHeldNote);
        mouseHeldNote = -1;
    }
    if (currentMouseNote != -1 && currentMouseNote != mouseHeldNote) {
        NoteOn(currentMouseNote);
        mouseHeldNote = currentMouseNote;
    }
}
