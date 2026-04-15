#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include "synth_globals.h"

// --- Voice management ---
void UpdateOscillatorsTable();
void UpdateEnvelopes();
void UpdateFilters();

// --- Note events ---
void NoteOn(int note);
void NoteOff(int note);

// --- Mod matrix queries ---
float GetModSum(int targetId);
float GetModAmountForUI(int targetId);

// --- MIDI IO ---
void RefreshMidiPorts();
void OpenMidiPort(int portIndex);

extern HMIDIIN              hMidiIn;
extern std::vector<std::string> midiPorts;
extern int                  selectedMidiPort;

// --- Audio render callback — passed to WasapiAudioDriver ---
void data_callback(float* pOut, int frameCount);
