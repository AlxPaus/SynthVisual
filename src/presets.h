#pragma once
#include <string>

std::string SaveFileDialog();
std::string OpenFileDialog();
void SavePreset(const std::string& filename);
void LoadPreset(const std::string& filename);
