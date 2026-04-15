#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include "synth/synth_globals.h"

void UpdateOscillatorsTable();
void UpdateEnvelopes();
void UpdateFilters();

void NoteOn(int note);
void NoteOff(int note);

float GetModSum(int targetId);
float GetModAmountForUI(int targetId);

void RefreshMidiPorts();
void OpenMidiPort(int portIndex);

extern HMIDIIN              hMidiIn;
extern std::vector<std::string> midiPorts;
extern int                  selectedMidiPort;

void data_callback(float* pOut, int frameCount);
