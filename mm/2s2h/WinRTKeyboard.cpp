#ifdef _UWP
#include "WinRTKeyboard.h"

#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Foundation.h>
#include <imgui.h>
#include <SDL2/SDL.h>

std::vector<uint32_t> g_char_buffer;
std::mutex g_buffer_mutex;
static winrt::Windows::UI::Core::CoreWindow::CharacterReceived_revoker g_characterReceivedRevoker;
static bool g_keyboardInitialized = false;

void ShowKeyboard()
{
  auto inputPane = winrt::Windows::UI::ViewManagement::InputPane::GetForCurrentView();
  inputPane.TryShow();
  SDL_StartTextInput();
}

void HideKeyboard()
{
  auto inputPane = winrt::Windows::UI::ViewManagement::InputPane::GetForCurrentView();
  inputPane.TryHide();
  SDL_StopTextInput();
}

void HandleCharacter(uint32_t keycode)
{
  std::unique_lock lk(g_buffer_mutex);
  g_char_buffer.push_back(keycode);
}

void ProcessCharacterBuffer()
{
  std::vector<uint32_t> charsToProcess;
  {
    std::unique_lock lk(g_buffer_mutex);
    if (g_char_buffer.empty()) {
      return;
    }
    charsToProcess = g_char_buffer;
    g_char_buffer.clear();
  }

  ImGuiIO& io = ImGui::GetIO();
  for (uint32_t codepoint : charsToProcess) {
    // Handle special keys
    if (codepoint == '\r') {
      codepoint = '\n'; // Convert carriage return to newline
    }
    if (codepoint == '\b') {
      continue;
    }
    io.AddInputCharacter(codepoint);
  }
}

void InitializeKeyboardInput()
{
  if (g_keyboardInitialized) {
    return;
  }

  try {
    auto coreWindow = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
    if (coreWindow) {
      g_characterReceivedRevoker = coreWindow.CharacterReceived(
        winrt::auto_revoke,
        [](winrt::Windows::UI::Core::CoreWindow const& sender,
           winrt::Windows::UI::Core::CharacterReceivedEventArgs const& args) {
          uint32_t keyCode = args.KeyCode();
          HandleCharacter(keyCode);
        }
      );
      g_keyboardInitialized = true;
    }
  } catch (...) {
    // CoreWindow might not be available yet, will retry later
  }
}
#endif