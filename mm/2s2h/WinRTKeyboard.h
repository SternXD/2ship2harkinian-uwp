#ifdef _UWP
#pragma once

#include <string>
#include <vector>
#include <mutex>

void ShowKeyboard(); // Show the keyboard
void HideKeyboard(); // Hide the keyboard
void HandleCharacter(uint32_t keycode); // Handle a single character input
void ProcessCharacterBuffer(); // Process buffered characters and inject into ImGui
void InitializeKeyboardInput(); // Initialize CoreWindow text input hooks

extern std::vector<uint32_t> g_char_buffer;
extern std::mutex g_buffer_mutex;
#endif