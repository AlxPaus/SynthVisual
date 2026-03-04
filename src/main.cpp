#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>

#include "miniaudio.h"
#include "osc.h" 

const int MAX_VOICES = 128; 

std::vector<WavetableOscillator> voicesA;
std::vector<WavetableOscillator> voicesB;
WavetableManager wtManager;

// === ГЛОБАЛЬНАЯ КЛАВИАТУРА ===
int keyboardOctave = 4;

// === SERUM PARAMETERS OSC A ===
bool oscAEnabled = true; int currentTableIndexA = 0; 
int oscA_Octave = 0; int pitchSemiA = 0; 
float wtPosA = 0.0f; int unisonVoicesA = 1; float unisonDetuneA = 0.15f; float unisonBlendA = 0.75f; float oscALevel = 0.75f; 

// === SERUM PARAMETERS OSC B ===
bool oscBEnabled = false; int currentTableIndexB = 0; 
int oscB_Octave = 0; int pitchSemiB = 0; 
float wtPosB = 0.0f; int unisonVoicesB = 1; float unisonDetuneB = 0.15f; float unisonBlendB = 0.75f; float oscBLevel = 0.75f; 

float globalPhase = 0.0f; float globalPhaseRand = 100.0f; float masterVolumeDb = 0.0f; 
float envA = 0.05f; float envD = 0.50f; float envS = 0.70f; float envR = 0.30f;
bool filterEnabled = false; int filterType = 0; float filterCutoff = 2000.0f; float filterResonance = 1.0f; 

// === LFO ===
enum LfoShape { LFO_SINE, LFO_TRIANGLE, LFO_SAW, LFO_SQUARE };
class AdvancedLFO {
public:
    float phase = 0.0f; float rateHz = 1.0f; float sampleRate = 44100.0f; float currentValue = 0.0f;
    LfoShape shape = LFO_TRIANGLE;

    void advance(int frames) {
        phase += (rateHz / sampleRate) * frames;
        while (phase >= 1.0f) phase -= 1.0f;
        
        if (shape == LFO_SINE) currentValue = std::sin(phase * 2.0f * 3.14159265f);
        else if (shape == LFO_TRIANGLE) {
            if (phase < 0.25f) currentValue = phase * 4.0f;
            else if (phase < 0.75f) currentValue = 1.0f - (phase - 0.25f) * 4.0f;
            else currentValue = -1.0f + (phase - 0.75f) * 4.0f;
        }
        else if (shape == LFO_SAW) currentValue = 1.0f - (phase * 2.0f);
        else if (shape == LFO_SQUARE) currentValue = (phase < 0.5f) ? 1.0f : -1.0f;
    }
} globalLFO;

int lfoSyncMode = 0; float globalBpm = 120.0f; float lfoRateHz = 1.0f; int lfoRateBPMIndex = 2;   
int lfoTarget = 0; float lfoAmount = 0.5f;    
float currentModCutoff = 2000.0f; float currentModWtPosA = 0.0f; float currentModLevelA = 0.75f; float currentModWtPosB = 0.0f; float currentModLevelB = 0.75f;

// === МАССИВЫ КЛАВИАТУРЫ ===
bool noteState[512] = { false }; 
int keysPressedMidi[512]; 

// === ЭФФЕКТЫ ===
struct DistortionFX { 
    bool enabled=false; float drive=0.0f; float mix=0.5f; 
    float process(float in){ if(!enabled) return in; float clean=in; float df=1.0f+drive*10.0f; float wet=std::atan(in*df)/std::atan(df); return clean*(1.0f-mix)+wet*mix; } 
} globalDistortion;

struct DelayFX { 
    bool enabled=false; float time=0.3f; float feedback=0.4f; float mix=0.3f; int writePos=0; std::vector<float> bufL, bufR; 
    DelayFX(){bufL.resize(88200,0);bufR.resize(88200,0);} 
    void process(float& inL, float& inR, float sr){ 
        if(!enabled) return; 
        int del=(int)(time*sr); if(del<=0) del=1; 
        int rp=writePos-del; 
        if(rp<0) rp+=(int)bufL.size(); 
        float oL=bufL[rp], oR=bufR[rp]; 
        bufL[writePos]=inL+oL*feedback; bufR[writePos]=inR+oR*feedback; 
        writePos=(writePos+1)%bufL.size(); 
        inL=inL*(1.0f-mix)+oL*mix; inR=inR*(1.0f-mix)+oR*mix; 
    } 
} globalDelay;

struct CombFilter { 
    std::vector<float> buf; int wp=0; float fb=0.8f, damping=0.2f, fs=0.0f; 
    void init(int sz){buf.resize(sz,0);} 
    float process(float in){ float out=buf[wp]; fs=(out*(1.0f-damping))+(fs*damping); buf[wp]=in+(fs*fb); wp=(wp+1)%buf.size(); return out; } 
};

struct AllPassFilter { 
    std::vector<float> buf; int wp=0; float fb=0.5f; 
    void init(int sz){buf.resize(sz,0);} 
    float process(float in){ float bo=buf[wp]; float out=-in+bo; buf[wp]=in+(bo*fb); wp=(wp+1)%buf.size(); return out; } 
};

struct ReverbFX { 
    bool enabled=false; 
    float roomSize=0.8f, damping=0.2f, mix=0.3f; 
    CombFilter cL[4], cR[4]; AllPassFilter aL[2], aR[2]; 
    ReverbFX(){ 
        int cs[4]={1557,1617,1491,1422}, as[2]={225,341}; 
        for(int i=0;i<4;++i){cL[i].init(cs[i]);cR[i].init(cs[i]+23);} 
        for(int i=0;i<2;++i){aL[i].init(as[i]);aR[i].init(as[i]+23);} 
    } 
    void process(float& inL, float& inR){ 
        if(!enabled) return; 
        float oL=0, oR=0, imL=inL*0.1f, imR=inR*0.1f; 
        for(int i=0;i<4;++i){ 
            cL[i].fb=roomSize; cL[i].damping=damping; 
            cR[i].fb=roomSize; cR[i].damping=damping; 
            oL+=cL[i].process(imL); oR+=cR[i].process(imR); 
        } 
        for(int i=0;i<2;++i){ oL=aL[i].process(oL); oR=aR[i].process(oR); } 
        inL=inL*(1.0f-mix)+oL*mix; inR=inR*(1.0f-mix)+oR*mix; 
    } 
} globalReverb;


// === ПАРАМЕТРЫ ОСЦИЛЛОГРАФА ===
const int SCOPE_SIZE = 512;
std::vector<float> scopeBuffer(SCOPE_SIZE, 0.0f);
int scopeIndex = 0;
bool scopeTriggered = false;
float lastScopeSample = 0.0f;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    float* pOut = (float*)pOutput;
    for (ma_uint32 i = 0; i < frameCount * 2; ++i) pOut[i] = 0.0f;
    
    if (lfoSyncMode == 1) { float beatHz = globalBpm / 60.0f; float mults[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f }; globalLFO.rateHz = beatHz * mults[lfoRateBPMIndex]; } 
    else { globalLFO.rateHz = lfoRateHz; }

    globalLFO.advance((int)frameCount);
    float lfoOut = globalLFO.currentValue * lfoAmount;

    currentModCutoff = filterCutoff; currentModWtPosA = wtPosA; currentModLevelA = oscALevel; currentModWtPosB = wtPosB; currentModLevelB = oscBLevel;

    if (lfoTarget == 1) currentModCutoff = std::max(20.0f, std::min(20000.0f, filterCutoff * std::pow(2.0f, lfoOut * 5.0f)));
    else if (lfoTarget == 2) currentModWtPosA = std::max(0.0f, std::min(1.0f, wtPosA + lfoOut));
    else if (lfoTarget == 3) currentModLevelA = std::max(0.0f, std::min(1.0f, oscALevel + lfoOut));
    else if (lfoTarget == 4) currentModWtPosB = std::max(0.0f, std::min(1.0f, wtPosB + lfoOut));
    else if (lfoTarget == 5) currentModLevelB = std::max(0.0f, std::min(1.0f, oscBLevel + lfoOut));

    voicesA[0].setWTPos(currentModWtPosA); voicesA[0].filter.setCoefficients((FilterType)filterType, currentModCutoff, filterResonance, 44100.0f);
    voicesB[0].setWTPos(currentModWtPosB); voicesB[0].filter.setCoefficients((FilterType)filterType, currentModCutoff, filterResonance, 44100.0f);

    for(size_t j = 1; j < voicesA.size(); ++j) { if (voicesA[j].isActive()) { voicesA[j].setWTPos(currentModWtPosA); voicesA[j].filter.setCoefficients((FilterType)filterType, currentModCutoff, filterResonance, 44100.0f); } }
    for(size_t j = 1; j < voicesB.size(); ++j) { if (voicesB[j].isActive()) { voicesB[j].setWTPos(currentModWtPosB); voicesB[j].filter.setCoefficients((FilterType)filterType, currentModCutoff, filterResonance, 44100.0f); } }

    float gcA = 1.0f / std::sqrt((float)unisonVoicesA); float gcB = 1.0f / std::sqrt((float)unisonVoicesB); float mGain = std::pow(10.0f, masterVolumeDb / 20.0f); 
    
    for (ma_uint32 i = 0; i < frameCount; ++i) {
        float mixL = 0.0f, mixR = 0.0f;
        if (oscAEnabled) { for (auto& v : voicesA) { if (v.isActive()) { float p = v.getPan(); float s = v.getSample() * currentModLevelA; mixL += s * (1.0f-p)*0.5f*gcA*0.15f; mixR += s * (1.0f+p)*0.5f*gcA*0.15f; } } }
        if (oscBEnabled) { for (auto& v : voicesB) { if (v.isActive()) { float p = v.getPan(); float s = v.getSample() * currentModLevelB; mixL += s * (1.0f-p)*0.5f*gcB*0.15f; mixR += s * (1.0f+p)*0.5f*gcB*0.15f; } } }
        mixL = globalDistortion.process(mixL); mixR = globalDistortion.process(mixR);
        globalDelay.process(mixL, mixR, 44100.0f); globalReverb.process(mixL, mixR); 
        
        float finalL = mixL * mGain;
        float finalR = mixR * mGain;
        
        pOut[2*i] = finalL; 
        pOut[2*i+1] = finalR;

        // ЗАПИСЬ В ОСЦИЛЛОГРАФ (Анализ моно-сигнала с триггером по нулю)
        float monoSample = (finalL + finalR) * 0.5f;
        if (!scopeTriggered && lastScopeSample <= 0.0f && monoSample > 0.0f) {
            scopeTriggered = true; // Поймали пересечение нуля (начало волны)
            scopeIndex = 0;
        }
        if (scopeTriggered && scopeIndex < SCOPE_SIZE) {
            scopeBuffer[scopeIndex++] = monoSample;
        }
        if (scopeIndex >= SCOPE_SIZE) {
            scopeTriggered = false; // Ждем следующего пересечения
        }
        lastScopeSample = monoSample;
    }
    (void)pInput; (void)pDevice;
}

void UpdateOscillatorsTable() { for(auto& v : voicesA) v.setWavetable(&wtManager.tables[currentTableIndexA]); for(auto& v : voicesB) v.setWavetable(&wtManager.tables[currentTableIndexB]); }
void UpdateEnvelopes() { for(auto& v : voicesA) v.env.setParameters(envA, envD, envS, envR); for(auto& v : voicesB) v.env.setParameters(envA, envD, envS, envR); }
void UpdateFilters() { for(auto& v : voicesA) v.filter.enabled = filterEnabled; for(auto& v : voicesB) v.filter.enabled = filterEnabled; }

void TriggerPool(std::vector<WavetableOscillator>& pool, int baseNote, int osc_Octave, int semiOffset, int unisonCount, float detune, float blend) {
    int voicesToTrigger = unisonCount;
    for (auto& v : pool) {
        if (!v.isActive()) {
            v.noteOn(baseNote); 
            float spreadStr = (unisonCount > 1) ? 2.0f * (float)(unisonCount - voicesToTrigger) / (float)(unisonCount - 1) - 1.0f : 0.0f;
            float detuneSemitones = spreadStr * (detune * 0.5f);
            float freq = 440.0f * std::pow(2.0f, (baseNote + (osc_Octave * 12) + semiOffset - 69.0f + detuneSemitones) / 12.0f);
            v.setFrequency(freq); v.setPan(spreadStr * blend);
            float basePhaseIndex = (globalPhase / 360.0f) * TABLE_SIZE; float randOffset = ((float)rand() / (float)RAND_MAX) * (globalPhaseRand / 100.0f) * TABLE_SIZE;
            v.setPhase(std::fmod(basePhaseIndex + randOffset, (float)TABLE_SIZE));
            voicesToTrigger--; if (voicesToTrigger <= 0) break; 
        }
    }
}

void NoteOn(int note) {
    if (note < 0 || note > 127) return;
    noteState[note] = true; 
    if (oscAEnabled) TriggerPool(voicesA, note, oscA_Octave, pitchSemiA, unisonVoicesA, unisonDetuneA, unisonBlendA);
    if (oscBEnabled) TriggerPool(voicesB, note, oscB_Octave, pitchSemiB, unisonVoicesB, unisonDetuneB, unisonBlendB);
}

void NoteOff(int note) {
    if (note < 0 || note > 127) return;
    noteState[note] = false; 
    for (auto& v : voicesA) if (v.getNote() == note) v.noteOff();
    for (auto& v : voicesB) if (v.getNote() == note) v.noteOff();
}

// === КРУТИЛКА ===
bool DrawKnob(const char* label, float* p_value, float v_min, float v_max, const char* format, float mod_val = 0.0f, bool logScale = false) {
    ImGuiIO& io = ImGui::GetIO(); float radius = 22.0f; ImVec2 pos = ImGui::GetCursorScreenPos(); ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);
    ImGui::InvisibleButton(label, ImVec2(radius*2, radius*2 + 20)); 
    bool value_changed = false;
    if (ImGui::IsItemActive() && io.MouseDelta.y != 0.0f) {
        float step = (v_max - v_min) * 0.005f; *p_value -= io.MouseDelta.y * step; *p_value = std::max(v_min, std::min(v_max, *p_value)); value_changed = true;
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList(); float angle_min = 3.141592f * 0.75f; float angle_max = 3.141592f * 2.25f;
    float t = logScale ? (std::log(*p_value/v_min) / std::log(v_max/v_min)) : ((*p_value - v_min) / (v_max - v_min));
    float angle = angle_min + (angle_max - angle_min) * t;
    draw_list->AddCircleFilled(center, radius, IM_COL32(40,40,45,255), 32); draw_list->AddCircle(center, radius, IM_COL32(15,15,15,255), 32, 2.0f);
    draw_list->PathArcTo(center, radius - 2.0f, angle_min, angle, 32); draw_list->PathStroke(IM_COL32(0,200,255,255), false, 4.0f);
    
    if (mod_val != 0.0f) {
        float mod_t = std::max(0.0f, std::min(1.0f, t + mod_val)); float angle_mod = angle_min + (angle_max - angle_min) * mod_t;
        if (mod_val > 0) draw_list->PathArcTo(center, radius + 4.0f, angle, angle_mod, 16); else draw_list->PathArcTo(center, radius + 4.0f, angle_mod, angle, 16);
        draw_list->PathStroke(IM_COL32(255,150,0,255), false, 2.0f);
    }
    
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        char buf[32]; snprintf(buf, 32, format, *p_value); ImVec2 tSize = ImGui::CalcTextSize(buf);
        draw_list->AddText(ImVec2(center.x - tSize.x/2, pos.y + radius*2 + 2), IM_COL32(255,255,255,255), buf);
    } else {
        ImVec2 tSize = ImGui::CalcTextSize(label); draw_list->AddText(ImVec2(center.x - tSize.x/2, pos.y + radius*2 + 2), IM_COL32(150,150,150,255), label);
    }
    return value_changed;
}

struct KeyMap { int key; int offset; };
std::vector<KeyMap> pcKeys = {
    {GLFW_KEY_Z, 0}, {GLFW_KEY_S, 1}, {GLFW_KEY_X, 2}, {GLFW_KEY_D, 3}, {GLFW_KEY_C, 4}, {GLFW_KEY_V, 5}, {GLFW_KEY_G, 6}, {GLFW_KEY_B, 7}, {GLFW_KEY_H, 8}, {GLFW_KEY_N, 9}, {GLFW_KEY_J, 10}, {GLFW_KEY_M, 11}, {GLFW_KEY_COMMA, 12}, {GLFW_KEY_L, 13}, {GLFW_KEY_PERIOD, 14}, {GLFW_KEY_SEMICOLON, 15}, {GLFW_KEY_SLASH, 16},
    {GLFW_KEY_Q, 12}, {GLFW_KEY_2, 13}, {GLFW_KEY_W, 14}, {GLFW_KEY_3, 15}, {GLFW_KEY_E, 16}, {GLFW_KEY_R, 17}, {GLFW_KEY_5, 18}, {GLFW_KEY_T, 19}, {GLFW_KEY_6, 20}, {GLFW_KEY_Y, 21}, {GLFW_KEY_7, 22}, {GLFW_KEY_U, 23}, {GLFW_KEY_I, 24}, {GLFW_KEY_9, 25}, {GLFW_KEY_O, 26}, {GLFW_KEY_0, 27}, {GLFW_KEY_P, 28}
};

bool PianoKey(const char* id, bool isBlack, bool isActive, ImVec2 size) {
    ImVec4 colorNorm = isBlack ? ImVec4(0.12f, 0.12f, 0.12f, 1.0f) : ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    ImVec4 colorActive = ImVec4(0.0f, 0.8f, 0.8f, 1.0f); 
    ImGui::PushStyleColor(ImGuiCol_Button, isActive ? colorActive : colorNorm);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isActive ? colorActive : (isBlack ? ImVec4(0.3f, 0.3f, 0.3f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colorActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::Button(id, size);
    bool isHoveredAndActive = ImGui::IsItemActive();
    ImGui::PopStyleColor(4); ImGui::PopStyleVar();
    return isHoveredAndActive;
}

void Draw3DTable(ImDrawList* drawList, ImVec2 pos, float viewW, float viewH, const Wavetable3D& table, float currentWtPos, float currentLevel, const std::vector<float>& activeWave, bool enabled) {
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

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0); glfwWindowHint(GLFW_SAMPLES, 4); 
    GLFWwindow* window = glfwCreateWindow(1280, 980, "Physics Synth: Oscilloscope", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window); glfwSwapInterval(1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui_ImplGlfw_InitForOpenGL(window, true); ImGui_ImplOpenGL3_Init("#version 130");
    ImGui::StyleColorsDark();
    
    for(int i=0; i<MAX_VOICES; ++i) { voicesA.emplace_back(44100.0f); voicesB.emplace_back(44100.0f); }
    UpdateOscillatorsTable(); UpdateEnvelopes(); UpdateFilters();
    
    for (int i=0; i<512; i++) keysPressedMidi[i] = -1; 

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32; config.playback.channels = 2; config.sampleRate = 44100; config.dataCallback = data_callback;
    ma_device device; ma_device_init(NULL, &config, &device); ma_device_start(&device);
    
    int mouseHeldNote = -1; 

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        for (const auto& k : pcKeys) {
            bool isDown = (glfwGetKey(window, k.key) == GLFW_PRESS);
            if (isDown && keysPressedMidi[k.key] == -1) { 
                int midiNote = ((keyboardOctave + 1) * 12) + k.offset; 
                if (midiNote < 0) midiNote = 0; if (midiNote > 127) midiNote = 127;
                keysPressedMidi[k.key] = midiNote;
                NoteOn(midiNote); 
            } 
            else if (!isDown && keysPressedMidi[k.key] != -1) { 
                NoteOff(keysPressedMidi[k.key]); 
                keysPressedMidi[k.key] = -1; 
            }
        }
        
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Synth UI", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

        // --- GLOBAL SECTION ---
        ImGui::BeginGroup(); 
        ImGui::Text("GLOBAL KEYBOARD"); ImGui::PushItemWidth(100); 
        ImGui::DragInt("OCTAVE", &keyboardOctave, 0.1f, 1, 7); ImGui::PopItemWidth(); 
        ImGui::EndGroup();
        ImGui::SameLine(0, 30);
        ImGui::BeginGroup(); ImGui::Text("MASTER"); 
        DrawKnob("Vol", &masterVolumeDb, -36.0f, 12.0f, "%.1f dB");
        ImGui::EndGroup();
        ImGui::Separator();

        if (ImGui::BeginTabBar("SerumTabs")) {
            if (ImGui::BeginTabItem("OSC")) {
                
                // === OSC A PANEL ===
                ImGui::PushID("OSC_A");
                ImGui::BeginGroup(); 
                ImGui::Checkbox("OSC A", &oscAEnabled); ImGui::SameLine(); ImGui::PushItemWidth(150);
                if (ImGui::ArrowButton("##leftWA", ImGuiDir_Left)) { currentTableIndexA--; if (currentTableIndexA < 0) currentTableIndexA = (int)wtManager.tables.size() - 1; UpdateOscillatorsTable(); } ImGui::SameLine();
                ImGui::Button(wtManager.tables[currentTableIndexA].name.c_str(), ImVec2(150, 0)); ImGui::SameLine();
                if (ImGui::ArrowButton("##rightWA", ImGuiDir_Right)) { currentTableIndexA++; if (currentTableIndexA >= (int)wtManager.tables.size()) currentTableIndexA = 0; UpdateOscillatorsTable(); } ImGui::PopItemWidth();
                
                ImGui::SameLine(0, 20); 
                float modWTP_A = (lfoTarget == 2) ? (globalLFO.currentValue * lfoAmount) : 0.0f;
                if (DrawKnob("WT POS", &wtPosA, 0.0f, 1.0f, "%.2f", modWTP_A)) UpdateOscillatorsTable();
                
                ImGui::SameLine(0, 10); 
                ImGui::BeginGroup(); ImGui::Text("PITCH"); ImGui::PushItemWidth(40); ImGui::DragInt("OCT", &oscA_Octave, 0.1f, -4, 4); ImGui::DragInt("SEM", &pitchSemiA, 0.1f, -12, 12); ImGui::PopItemWidth(); ImGui::EndGroup();
                
                ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("UNISON"); ImGui::PushItemWidth(40); ImGui::DragInt("##UniA", &unisonVoicesA, 0.2f, 1, 16); ImGui::PopItemWidth(); ImGui::EndGroup();
                ImGui::SameLine(0, 10); DrawKnob("Detune", &unisonDetuneA, 0.0f, 1.0f, "%.2f");
                ImGui::SameLine(0, 10); DrawKnob("Blend", &unisonBlendA, 0.0f, 1.0f, "%.2f");
                
                float modLev_A = (lfoTarget == 3) ? (globalLFO.currentValue * lfoAmount) : 0.0f;
                ImGui::SameLine(0, 10); DrawKnob("Level", &oscALevel, 0.0f, 1.0f, "%.2f", modLev_A);
                ImGui::EndGroup(); 
                ImGui::PopID(); 
                ImGui::Separator();

                // === OSC B PANEL ===
                ImGui::PushID("OSC_B");
                ImGui::BeginGroup(); 
                ImGui::Checkbox("OSC B", &oscBEnabled); ImGui::SameLine(); ImGui::PushItemWidth(150);
                if (ImGui::ArrowButton("##leftWB", ImGuiDir_Left)) { currentTableIndexB--; if (currentTableIndexB < 0) currentTableIndexB = (int)wtManager.tables.size() - 1; UpdateOscillatorsTable(); } ImGui::SameLine();
                ImGui::Button(wtManager.tables[currentTableIndexB].name.c_str(), ImVec2(150, 0)); ImGui::SameLine();
                if (ImGui::ArrowButton("##rightWB", ImGuiDir_Right)) { currentTableIndexB++; if (currentTableIndexB >= (int)wtManager.tables.size()) currentTableIndexB = 0; UpdateOscillatorsTable(); } ImGui::PopItemWidth();
                
                ImGui::SameLine(0, 20); 
                float modWTP_B = (lfoTarget == 4) ? (globalLFO.currentValue * lfoAmount) : 0.0f;
                if (DrawKnob("WT POS", &wtPosB, 0.0f, 1.0f, "%.2f", modWTP_B)) UpdateOscillatorsTable();
                
                ImGui::SameLine(0, 10); 
                ImGui::BeginGroup(); ImGui::Text("PITCH"); ImGui::PushItemWidth(40); ImGui::DragInt("OCT", &oscB_Octave, 0.1f, -4, 4); ImGui::DragInt("SEM", &pitchSemiB, 0.1f, -12, 12); ImGui::PopItemWidth(); ImGui::EndGroup();
                
                ImGui::SameLine(0, 10); ImGui::BeginGroup(); ImGui::Text("UNISON"); ImGui::PushItemWidth(40); ImGui::DragInt("##UniB", &unisonVoicesB, 0.2f, 1, 16); ImGui::PopItemWidth(); ImGui::EndGroup();
                ImGui::SameLine(0, 10); DrawKnob("Detune", &unisonDetuneB, 0.0f, 1.0f, "%.2f");
                ImGui::SameLine(0, 10); DrawKnob("Blend", &unisonBlendB, 0.0f, 1.0f, "%.2f");
                
                float modLev_B = (lfoTarget == 5) ? (globalLFO.currentValue * lfoAmount) : 0.0f;
                ImGui::SameLine(0, 10); DrawKnob("Level", &oscBLevel, 0.0f, 1.0f, "%.2f", modLev_B);
                ImGui::EndGroup(); 
                ImGui::PopID(); 
                ImGui::Separator();

                // === ENV & FILTER ===
                ImGui::BeginGroup(); 
                ImGui::PushID("ENV1");
                ImGui::BeginGroup(); ImGui::Text("ENV 1 (AMP)"); bool envC = false; 
                ImGui::SameLine(0, 20); if (DrawKnob("A", &envA, 0.001f, 5.0f, "%.2fs")) envC=true; 
                ImGui::SameLine(0, 10); if (DrawKnob("D", &envD, 0.001f, 5.0f, "%.2fs")) envC=true; 
                ImGui::SameLine(0, 10); if (DrawKnob("S", &envS, 0.0f, 1.0f, "%.2f")) envC=true; 
                ImGui::SameLine(0, 10); if (DrawKnob("R", &envR, 0.001f, 5.0f, "%.2fs")) envC=true; 
                if (envC) UpdateEnvelopes();
                
                ImVec2 envPos = ImGui::GetCursorScreenPos(); 
                ImVec2 envSize(260.0f, 80.0f); 
                ImGui::InvisibleButton("##EnvGraph", envSize); 
                ImDrawList* ed = ImGui::GetWindowDrawList(); 
                ed->AddRectFilled(envPos, ImVec2(envPos.x + envSize.x, envPos.y + envSize.y), IM_COL32(30, 30, 35, 255)); 
                
                float tt = 10.0f; 
                float pxA = (envA / tt) * envSize.x * 2.0f; 
                float pxD = (envD / tt) * envSize.x * 2.0f; 
                float pxS = 40.0f; 
                float pxR = (envR / tt) * envSize.x * 2.0f; 
                
                ImVec2 p0(envPos.x, envPos.y + envSize.y);
                ImVec2 p1(p0.x + pxA, envPos.y);
                ImVec2 p2(p1.x + pxD, envPos.y + envSize.y - (envS * envSize.y));
                ImVec2 p3(p2.x + pxS, p2.y);
                ImVec2 p4(p3.x + pxR, envPos.y + envSize.y); 

                // Только Отрисовка (Без перетаскивания)
                ed->AddLine(p0, p1, IM_COL32(0, 200, 255, 255), 2.0f); 
                ed->AddLine(p1, p2, IM_COL32(0, 200, 255, 255), 2.0f); 
                ed->AddLine(p2, p3, IM_COL32(0, 200, 255, 255), 2.0f); 
                ed->AddLine(p3, p4, IM_COL32(0, 200, 255, 255), 2.0f);
                ed->AddCircleFilled(p1, 4.0f, IM_COL32(255, 255, 255, 255)); 
                ed->AddCircleFilled(p2, 4.0f, IM_COL32(255, 255, 255, 255)); 
                ed->AddCircleFilled(p4, 4.0f, IM_COL32(255, 255, 255, 255));

                int activeVoiceIdx = -1; float maxLvl = -1.0f;
                for (int i = 0; i < MAX_VOICES; ++i) { if (voicesA[i].isActive() && voicesA[i].env.currentLevel > maxLvl) { maxLvl = voicesA[i].env.currentLevel; activeVoiceIdx = i; } }
                if (activeVoiceIdx != -1) {
                    ADSR& aEnv = voicesA[activeVoiceIdx].env; float lvl = aEnv.currentLevel; float dotY = envPos.y + envSize.y - (lvl * envSize.y); float dotX = envPos.x;
                    if (aEnv.state == ENV_ATTACK) dotX = p0.x + pxA * lvl; else if (aEnv.state == ENV_DECAY) { float r = 1.0f - envS; dotX = p1.x + pxD * ((r > 0.001f) ? ((1.0f - lvl)/r) : 1.0f); }
                    else if (aEnv.state == ENV_SUSTAIN) dotX = p2.x + pxS * 0.5f; else if (aEnv.state == ENV_RELEASE) { float t = (envS > 0.001f) ? (1.0f - (lvl / envS)) : 1.0f; dotX = p3.x + pxR * std::max(0.0f, std::min(1.0f, t)); }
                    ed->AddCircleFilled(ImVec2(dotX, dotY), 4.5f, IM_COL32(255, 215, 0, 255));
                }
                ImGui::EndGroup(); ImGui::PopID();

                ImGui::SameLine(0, 40); 
                ImGui::PushID("FILTER1");
                ImGui::BeginGroup(); ImGui::Text("FILTER"); ImGui::BeginGroup(); bool filterChanged = false;
                if (ImGui::Checkbox("##FiltEnable", &filterEnabled)) filterChanged = true; ImGui::SameLine(); ImGui::PushItemWidth(150);
                const char* filterTypes[] = { "Low Pass 12dB", "High Pass 12dB", "Band Pass 12dB" };
                if (ImGui::Combo("##FiltType", &filterType, filterTypes, 3)) filterChanged = true; ImGui::PopItemWidth();
                
                float modCutoff = (lfoTarget == 1) ? (globalLFO.currentValue * lfoAmount) : 0.0f;
                ImGui::SameLine(0, 20); if (DrawKnob("Cutoff", &filterCutoff, 20.0f, 20000.0f, "%.0f Hz", modCutoff, true)) filterChanged = true;
                ImGui::SameLine(0, 10); if (DrawKnob("Res", &filterResonance, 0.1f, 10.0f, "%.2f Q")) filterChanged = true;
                if (filterChanged) UpdateFilters(); ImGui::EndGroup(); 

                ImGui::SameLine(0, 20); 
                ImVec2 filtPos = ImGui::GetCursorScreenPos(); ImVec2 filtSize(250.0f, 80.0f); ImGui::InvisibleButton("##FiltGraph", filtSize); ImDrawList* fd = ImGui::GetWindowDrawList();
                fd->AddRectFilled(filtPos, ImVec2(filtPos.x + filtSize.x, filtPos.y + filtSize.y), IM_COL32(30, 30, 35, 255));
                if (filterEnabled) {
                    std::vector<ImVec2> pts; int numPoints = (int)filtSize.x;
                    for (int i = 0; i < numPoints; ++i) {
                        float freq = 20.0f * std::pow(1000.0f, (float)i / (numPoints - 1.0f)); 
                        float magDb = 20.0f * std::log10(std::max(0.0001f, voicesA[0].filter.getMagnitude(freq, 44100.0f)));
                        pts.push_back(ImVec2(filtPos.x + i, filtPos.y + std::max(0.0f, std::min(1.0f, 1.0f - (magDb + 24.0f) / 42.0f)) * filtSize.y));
                    }
                    for (size_t i = 0; i < pts.size() - 1; ++i) fd->AddLine(pts[i], pts[i+1], IM_COL32(0, 255, 100, 255), 2.0f);
                    float xCut = filtPos.x + (std::log(currentModCutoff / 20.0f) / std::log(1000.0f)) * filtSize.x;
                    if (xCut >= filtPos.x && xCut <= filtPos.x + filtSize.x) {
                        fd->AddLine(ImVec2(xCut, filtPos.y), ImVec2(xCut, filtPos.y + filtSize.y), IM_COL32(255, 255, 255, 50), 1.0f);
                        float yNorm = 1.0f - (20.0f * std::log10(std::max(0.0001f, voicesA[0].filter.getMagnitude(currentModCutoff, 44100.0f))) + 24.0f) / 42.0f;
                        fd->AddCircleFilled(ImVec2(xCut, filtPos.y + std::max(0.0f, std::min(1.0f, yNorm)) * filtSize.y), 4.0f, IM_COL32(255, 255, 255, 255));
                    }
                } else {
                    fd->AddLine(ImVec2(filtPos.x, filtPos.y + (1.0f - 24.0f / 42.0f) * filtSize.y), ImVec2(filtPos.x + filtSize.x, filtPos.y + (1.0f - 24.0f / 42.0f) * filtSize.y), IM_COL32(100, 100, 100, 255), 2.0f);
                }
                ImGui::EndGroup(); ImGui::PopID();
                ImGui::EndGroup(); ImGui::Separator();

                // === LFO ===
                ImGui::BeginGroup(); ImGui::Text("LFO 1"); ImGui::PushItemWidth(80);
                const char* lfoShapes[] = { "Sine", "Triangle", "Saw", "Square" };
                ImGui::Combo("Shape##lfo", (int*)&globalLFO.shape, lfoShapes, 4); ImGui::SameLine();
                const char* syncModes[] = { "Hz", "BPM" }; ImGui::Combo("Sync##lfo", &lfoSyncMode, syncModes, 2);
                if (lfoSyncMode == 1) { ImGui::SameLine(); ImGui::DragFloat("BPM##lfo", &globalBpm, 1.0f, 20.0f, 300.0f, "%.1f"); ImGui::SameLine(); const char* rates[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }; ImGui::Combo("Rate##lfo", &lfoRateBPMIndex, rates, 6); } 
                else { ImGui::SameLine(); ImGui::SliderFloat("Rate##lfo", &lfoRateHz, 0.05f, 20.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic); }
                ImGui::SameLine(0, 40); const char* targets[] = { "None", "Cutoff", "OSC A WTPOS", "OSC A Level", "OSC B WTPOS", "OSC B Level" }; ImGui::Combo("Target##lfo", &lfoTarget, targets, 6); ImGui::SameLine();
                ImGui::SliderFloat("Amount##lfo", &lfoAmount, -1.0f, 1.0f, "%.2f"); 
                ImGui::SameLine(); ImGui::ProgressBar((globalLFO.currentValue + 1.0f) / 2.0f, ImVec2(100, 20), "LFO Out");
                ImGui::PopItemWidth(); ImGui::EndGroup(); ImGui::Separator();

                // === 3D RENDER ===
                ImVec2 pos3d=ImGui::GetCursorScreenPos(); float totalW=ImGui::GetContentRegionAvail().x; float viewH3d=160.0f; ImGui::InvisibleButton("##3DViewSplit", ImVec2(totalW, viewH3d)); ImDrawList* drawList = ImGui::GetWindowDrawList(); 
                Draw3DTable(drawList, pos3d, totalW/2.0f - 5.0f, viewH3d, wtManager.tables[currentTableIndexA], currentModWtPosA, currentModLevelA, voicesA[0].getWavetableData(), oscAEnabled);
                Draw3DTable(drawList, ImVec2(pos3d.x + totalW/2.0f + 5.0f, pos3d.y), totalW/2.0f - 5.0f, viewH3d, wtManager.tables[currentTableIndexB], currentModWtPosB, currentModLevelB, voicesB[0].getWavetableData(), oscBEnabled);
                
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("FX")) {
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                ImGui::PushID("FX_DIST"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255));
                ImGui::BeginChild("DistortionPanel", ImVec2(0, 100), true); ImGui::Checkbox("DISTORTION", &globalDistortion.enabled); ImGui::SameLine(150);
                DrawKnob("Drive", &globalDistortion.drive, 0.0f, 1.0f, "%.2f"); ImGui::SameLine(250); DrawKnob("Mix", &globalDistortion.mix, 0.0f, 1.0f, "%.2f");
                ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();
                
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                ImGui::PushID("FX_DELAY"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255));
                ImGui::BeginChild("DelayPanel", ImVec2(0, 100), true); ImGui::Checkbox("DELAY", &globalDelay.enabled); ImGui::SameLine(150);
                DrawKnob("Time", &globalDelay.time, 0.01f, 1.5f, "%.2fs"); ImGui::SameLine(250); DrawKnob("Fback", &globalDelay.feedback, 0.0f, 0.95f, "%.2f"); ImGui::SameLine(350); DrawKnob("Mix", &globalDelay.mix, 0.0f, 1.0f, "%.2f");
                ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();
                
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                ImGui::PushID("FX_REV"); ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 35, 255));
                ImGui::BeginChild("ReverbPanel", ImVec2(0, 100), true); ImGui::Checkbox("REVERB", &globalReverb.enabled); ImGui::SameLine(150);
                DrawKnob("Size", &globalReverb.roomSize, 0.0f, 0.98f, "%.2f"); ImGui::SameLine(250); DrawKnob("Damp", &globalReverb.damping, 0.0f, 1.0f, "%.2f"); ImGui::SameLine(350); DrawKnob("Mix", &globalReverb.mix, 0.0f, 1.0f, "%.2f");
                ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopID();

                ImGui::Dummy(ImVec2(0.0f, 60.0f));
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();

        // === ОСЦИЛЛОГРАФ ===
        ImVec2 scopePos = ImGui::GetCursorScreenPos();
        float scopeW = ImGui::GetContentRegionAvail().x;
        float scopeH = 60.0f;
        ImGui::InvisibleButton("##MainScope", ImVec2(scopeW, scopeH));
        ImDrawList* scopeDraw = ImGui::GetWindowDrawList();
        scopeDraw->AddRectFilled(scopePos, ImVec2(scopePos.x + scopeW, scopePos.y + scopeH), IM_COL32(15, 15, 20, 255));
        
        float midY = scopePos.y + scopeH * 0.5f;
        scopeDraw->AddLine(ImVec2(scopePos.x, midY), ImVec2(scopePos.x + scopeW, midY), IM_COL32(40, 40, 50, 255), 1.0f); 
        
        for (int i = 0; i < SCOPE_SIZE - 1; ++i) {
            float x1 = scopePos.x + ((float)i / (SCOPE_SIZE - 1)) * scopeW;
            float x2 = scopePos.x + ((float)(i + 1) / (SCOPE_SIZE - 1)) * scopeW;
            float y1 = midY - std::max(-1.0f, std::min(1.0f, scopeBuffer[i])) * (scopeH * 0.45f);
            float y2 = midY - std::max(-1.0f, std::min(1.0f, scopeBuffer[i+1])) * (scopeH * 0.45f);
            scopeDraw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 255, 150, 255), 1.5f);
        }
        ImGui::Separator();

        // === ПИАНИНО ===
        ImGui::BeginChild("PianoScroll",ImVec2(0,200),true,ImGuiWindowFlags_HorizontalScrollbar);float whiteW=36.0f,whiteH=150.0f,blackW=22.0f,blackH=90.0f,spacing=1.0f;int startOct=2,endOct=7,currentMouseNote=-1;ImVec2 startPosP=ImGui::GetCursorPos();int whiteIndex=0;for(int oct=startOct;oct<=endOct;++oct){for(int note=0;note<12;++note){if(note==1||note==3||note==6||note==8||note==10)continue;int midi=(oct+1)*12+note;ImGui::SetCursorPos(ImVec2(startPosP.x+whiteIndex*(whiteW+spacing),startPosP.y));ImGui::PushID(midi);if(PianoKey("##w",false,noteState[midi],ImVec2(whiteW,whiteH)))currentMouseNote=midi;ImGui::PopID();if(note==0){ImGui::SetCursorPos(ImVec2(startPosP.x+whiteIndex*(whiteW+spacing)+8,startPosP.y+whiteH-20));ImGui::TextColored(ImVec4(0.2f,0.2f,0.2f,1.0f),"C%d",oct);}whiteIndex++;}}whiteIndex=0;for(int oct=startOct;oct<=endOct;++oct){for(int note=0;note<12;++note){bool isBlack=(note==1||note==3||note==6||note==8||note==10);if(!isBlack){whiteIndex++;continue;}int midi=(oct+1)*12+note;float offsetX=startPosP.x+whiteIndex*(whiteW+spacing)-(blackW/2.0f)-(spacing/2.0f);ImGui::SetCursorPos(ImVec2(offsetX,startPosP.y));ImGui::PushID(midi);if(PianoKey("##b",true,noteState[midi],ImVec2(blackW,blackH)))currentMouseNote=midi;ImGui::PopID();}}ImGui::SetCursorPos(ImVec2(startPosP.x,startPosP.y+whiteH+10));ImGui::EndChild();if(mouseHeldNote!=-1&&mouseHeldNote!=currentMouseNote){if(!keysPressedMidi[mouseHeldNote])NoteOff(mouseHeldNote);mouseHeldNote=-1;}if(currentMouseNote!=-1&&currentMouseNote!=mouseHeldNote){NoteOn(currentMouseNote);mouseHeldNote=currentMouseNote;}

        ImGui::End(); ImGui::Render(); int w, h; glfwGetFramebufferSize(window, &w, &h); glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); glfwSwapBuffers(window);
    }
    ma_device_uninit(&device); ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate(); return 0;
}