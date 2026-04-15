#define NOMINMAX
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>

#include "audio/wasapi_audio.h"
#include "synth/synth_globals.h"
#include "synth/audio_engine.h"
#include "ui/ui.h"

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(1280, 950, "Osci: Wave Editor & Synth", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    ImGui::StyleColorsDark();

    for (int i = 0; i < MAX_VOICES; ++i) {
        g_synth.voicesA.emplace_back(kSampleRate);
        g_synth.voicesB.emplace_back(kSampleRate);
    }
    UpdateOscillatorsTable();
    UpdateEnvelopes();
    UpdateFilters();
    RefreshMidiPorts();

    WasapiAudioDriver audioDriver;
    if (!audioDriver.init(data_callback)) {
        std::cerr << "Failed to initialize audio\n";
        return 1;
    }
    audioDriver.start();

    int         mouseHeldNote = -1;
    std::string currentPreset = "Init";

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        HandlePcKeyboard(window);
        UpdateSpectrum();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Synth UI", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

        RenderTopBar(currentPreset);
        ImGui::Separator();

        if (ImGui::BeginTabBar("SerumTabs")) {
            if (ImGui::BeginTabItem("OSC"))        { RenderOscTab();       ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("DRAW"))       { RenderDrawTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("FX"))         { RenderFxTab();        ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("MOD MATRIX")) { RenderModMatrixTab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        RenderScopeSection();
        ImGui::Separator();
        RenderPianoSection(mouseHeldNote);

        ImGui::End();
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    audioDriver.stop();
    if (hMidiIn) { midiInStop(hMidiIn); midiInClose(hMidiIn); }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
