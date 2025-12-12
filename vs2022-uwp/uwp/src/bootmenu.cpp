// Boot menu for ROM extraction before game initialization
// Heavily inspired by imgui's SDL + DX11 example: https://github.com/ocornut/imgui/blob/master/examples/example_sdl2_directx11/main.cpp
// imgui licensed under MIT Copyright (c) 2014-2025 Omar Cornut
#include "bootmenu.h"

#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <process.h>

#include <SDL2/SDL.h>

#include <imgui.h>
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_dx11.h"

#include "dx11glue.h"
#include "libuwp.h"

#include <Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.h>
#include <Windows.Storage.h>
#include <winrt/Windows.Storage.h>
#include <Windows.Foundation.h>
#include <winrt/Windows.Foundation.h>
#include <Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>

#include <io.h>
#include <fcntl.h>
#include <Windows.h>
#include <winbase.h>
#include <errno.h>

#include "Extractor/Extract.h"

extern "C" {
    typedef void (*ZapdProgressCallback)(const char* message);
    #ifdef _MSC_VER
    __declspec(dllimport) void SetZapdProgressCallback(ZapdProgressCallback callback);
    #else
    void SetZapdProgressCallback(ZapdProgressCallback callback);
    #endif
}

extern "C" __declspec(dllimport) bool uwp_pick_rom(char* outPath, size_t outLen);
extern "C" __declspec(dllimport) void uwp_GetBundlePath(char* buffer);
extern "C" __declspec(dllimport) const char* uwp_get_aux_root();
extern "C" __declspec(dllimport) bool Extractor_CallZapd(const char* installPath, const char* exportdir, const char* romPath);

namespace bootmenu
{
	enum class BootState
	{
		Setup,
		CheckingO2R,
		SelectingROM,
		Extracting,
		ExtractionComplete,
		ExtractionFailed,
		Ready
	};
	
	enum class StorageLocation
	{
		LocalState,
		DDrive,
		EDrive
	};

	struct ExtractionState
	{
		std::mutex mutex;
		std::atomic<BootState> state{ BootState::CheckingO2R };
		std::string statusMessage;
		std::string errorMessage;
		std::string selectedRomPath;
		bool extractionSuccess = false;
		std::chrono::steady_clock::time_point extractionStartTime;
		float progressPercent = 0.0f;
		std::vector<std::string> logLines;
		int currentFileIndex = 0;
		int totalFiles = 0;
	};

	static ExtractionState g_extractionState;
	
	static StorageLocation g_cachedStorageLocation = StorageLocation::DDrive;
	static bool g_storageLocationCached = false;

	namespace {
		StorageLocation GetStorageLocation() {
			// Return cached value if available
			if (g_storageLocationCached) {
				return g_cachedStorageLocation;
			}
			
			// Try to read from ApplicationData
			try {
				auto appData = winrt::Windows::Storage::ApplicationData::Current();
				if (!appData) {
					g_cachedStorageLocation = StorageLocation::DDrive;
					g_storageLocationCached = true;
					return g_cachedStorageLocation;
				}
				auto localSettings = appData.LocalSettings();
				if (!localSettings) {
					g_cachedStorageLocation = StorageLocation::DDrive;
					g_storageLocationCached = true;
					return g_cachedStorageLocation;
				}
				auto container = localSettings.Containers().TryLookup(L"Settings");
				if (container) {
					auto value = container.Values().TryLookup(L"StorageLocation");
					if (value) {
						int location = value.as<int>();
						g_cachedStorageLocation = static_cast<StorageLocation>(location);
						g_storageLocationCached = true;
						return g_cachedStorageLocation;
					}
				}
			} catch (...) {
				// Settings not available, use default
			}
			g_cachedStorageLocation = StorageLocation::DDrive; // Default to D: drive
			g_storageLocationCached = true;
			return g_cachedStorageLocation;
		}
		
		void SaveStorageLocation(StorageLocation location) {
			g_cachedStorageLocation = location;
			g_storageLocationCached = true;
			
			// Try to persist to ApplicationData
			try {
				auto appData = winrt::Windows::Storage::ApplicationData::Current();
				if (!appData) {
					return; // Can't save if ApplicationData not available
				}
				auto localSettings = appData.LocalSettings();
				if (!localSettings) {
					return;
				}
				auto container = localSettings.CreateContainer(L"Settings", winrt::Windows::Storage::ApplicationDataCreateDisposition::Always);
				if (!container) {
					return;
				}
				auto propertyValue = winrt::Windows::Foundation::PropertyValue::CreateInt32(static_cast<int>(location));
				winrt::Windows::Foundation::Collections::IPropertySet values = container.Values();
				values.Insert(L"StorageLocation", propertyValue);
			} catch (...) {
				// Failed to save settings
			}
		}
		
		std::filesystem::path GetAuxRoot() {
			StorageLocation location = GetStorageLocation();
			
			switch (location) {
				case StorageLocation::LocalState: {
					// Use UWP LocalState folder
					try {
						auto appData = winrt::Windows::Storage::ApplicationData::Current();
						if (!appData) {
							return std::filesystem::path("D:/2ship/");
						}
						auto localFolder = appData.LocalFolder();
						if (!localFolder) {
							return std::filesystem::path("D:/2ship/");
						}
						std::wstring localPath = localFolder.Path().c_str();
						std::string auxPath = std::filesystem::path(localPath).string() + "\\2ship";
						return std::filesystem::path(auxPath);
					} catch (...) {
						// Fallback to D: if LocalState fails
						return std::filesystem::path("D:/2ship/");
					}
				}
				case StorageLocation::DDrive:
					return std::filesystem::path("D:/2ship/");
				case StorageLocation::EDrive:
					return std::filesystem::path("E:/2ship/");
				default:
					return std::filesystem::path("D:/2ship/");
			}
		}
		
		std::string GetAppBundlePath() {
			char buffer[1024] = { 0 };
			uwp_GetBundlePath(buffer);
			return std::string(buffer);
		}
	}

	// Get progress messages from ZAPD
	void ZapdProgressCallbackImpl(const char* message) {
		if (!message) return;
		
		std::string fullMsg(message);
		if (fullMsg.empty()) return;
		
		std::vector<std::string> lines;
		std::string currentLine;
		
		for (size_t i = 0; i < fullMsg.length(); ++i) {
			char c = fullMsg[i];
			if (c == '\n' || c == '\r') {
				if (!currentLine.empty()) {
					while (!currentLine.empty() && (currentLine.back() == ' ' || currentLine.back() == '\t')) {
						currentLine.pop_back();
					}
					if (!currentLine.empty()) {
						lines.push_back(currentLine);
					}
					currentLine.clear();
				}
			} else {
				currentLine += c;
			}
		}
		
		if (!currentLine.empty()) {
			while (!currentLine.empty() && (currentLine.back() == ' ' || currentLine.back() == '\t')) {
				currentLine.pop_back();
			}
			if (!currentLine.empty()) {
				lines.push_back(currentLine);
			}
		}
		
		if (lines.empty()) return;
		
		{
			std::lock_guard<std::mutex> lock(g_extractionState.mutex);
			
			for (const auto& line : lines) {
				if (line.empty()) continue;
				
				if (line.find("Generated OTR") != std::string::npos || 
				    line.find("OTR File Data") != std::string::npos) {
					if (g_extractionState.totalFiles > 0) {
						g_extractionState.currentFileIndex = g_extractionState.totalFiles;
						g_extractionState.progressPercent = 100.0f;
					}
				}
				
				int current = 0, total = 0;
				if (sscanf_s(line.c_str(), "(%d / %d):", &current, &total) == 2 || 
				    sscanf_s(line.c_str(), "(%d/%d):", &current, &total) == 2) {
					if (total > 0 && current > 0 && current <= total) {
						g_extractionState.currentFileIndex = current;
						g_extractionState.totalFiles = total;
						g_extractionState.progressPercent = (static_cast<float>(current) / static_cast<float>(total)) * 100.0f;
					}
				}
				
				g_extractionState.logLines.push_back(line);
			}
			
			if (g_extractionState.logLines.size() > 500) {
				g_extractionState.logLines.erase(g_extractionState.logLines.begin(), 
					g_extractionState.logLines.begin() + (g_extractionState.logLines.size() - 500));
			}
		}
	}

	void ExtractionThreadWorker(const std::string& romPath, const std::string& installPath, const std::string& auxRoot)
	{
		auto AddLog = [](const std::string& msg) {
			std::lock_guard<std::mutex> lock(g_extractionState.mutex);
			g_extractionState.logLines.push_back(msg);
			if (g_extractionState.logLines.size() > 100) {
				g_extractionState.logLines.erase(g_extractionState.logLines.begin());
			}
		};

		{
			std::lock_guard<std::mutex> lock(g_extractionState.mutex);
			g_extractionState.state = BootState::Extracting;
			g_extractionState.progressPercent = 0.0f;
			g_extractionState.currentFileIndex = 0;
			g_extractionState.totalFiles = 0;
			g_extractionState.extractionStartTime = std::chrono::steady_clock::now();
		}
		AddLog("Initializing extraction...");
		
		// Set up callback to receive ZAPD output
		SetZapdProgressCallback(ZapdProgressCallbackImpl);

		auto progressThreadFunc = []() {
			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				
				BootState currentState;
				{
					std::lock_guard<std::mutex> lock(g_extractionState.mutex);
					currentState = g_extractionState.state;
					if (currentState != BootState::Extracting) {
						break; // Extraction finished
					}
					
					if (g_extractionState.totalFiles == 0) {
						auto elapsed = std::chrono::steady_clock::now() - g_extractionState.extractionStartTime;
						auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
						float timeProgress = (std::min)(95.0f, 5.0f + (elapsedSeconds / 180.0f) * 90.0f);
						g_extractionState.progressPercent = timeProgress;
					}
				}
			}
		};
		std::thread progressThread(progressThreadFunc);

		bool result = false;
		bool success = false;
		std::string errorDetails;
		
		try {
			result = Extractor_CallZapd(installPath.c_str(), auxRoot.c_str(), romPath.c_str());
			success = result;
		} catch (const std::runtime_error& e) {
			errorDetails = std::string("Runtime error: ") + e.what();
			AddLog(errorDetails);
			success = false;
		} catch (const std::exception& e) {
			errorDetails = std::string("Exception: ") + e.what();
			AddLog(errorDetails);
			success = false;
		} catch (...) {
			errorDetails = "Unknown exception occurred during extraction";
			AddLog(errorDetails);
			success = false;
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		
		std::string completionMessage;
		{
			std::lock_guard<std::mutex> lock(g_extractionState.mutex);
			
			if (g_extractionState.totalFiles > 0) {
				g_extractionState.currentFileIndex = g_extractionState.totalFiles;
				g_extractionState.progressPercent = 100.0f;
			} else if (g_extractionState.progressPercent < 100.0f) {
				g_extractionState.progressPercent = 100.0f;
			}
			
			g_extractionState.extractionSuccess = success;
			
			if (success)
			{
				const std::filesystem::path mmO2rPath = GetAuxRoot() / "mm.o2r";
				if (std::filesystem::exists(mmO2rPath))
				{
					const auto o2rSize = std::filesystem::file_size(mmO2rPath);
					if (o2rSize >= 1024 * 1024) // At least 1MB
					{
						g_extractionState.state = BootState::ExtractionComplete;
						g_extractionState.progressPercent = 100.0f;
						completionMessage = "Extraction completed successfully!";
					}
					else
					{
						g_extractionState.state = BootState::ExtractionFailed;
						g_extractionState.errorMessage = "Generated mm.o2r file is too small. Please try again.";
						completionMessage = g_extractionState.errorMessage;
					}
				}
				else
				{
					g_extractionState.state = BootState::ExtractionFailed;
					g_extractionState.errorMessage = "No mm.o2r was produced. Please try again.";
					completionMessage = g_extractionState.errorMessage;
				}
			}
			else
			{
				g_extractionState.state = BootState::ExtractionFailed;
				if (!errorDetails.empty()) {
					g_extractionState.errorMessage = "ZAPD extraction failed: " + errorDetails;
				} else {
					g_extractionState.errorMessage = "ZAPD extraction failed. Please check the logs for details.";
				}
				completionMessage = g_extractionState.errorMessage;
			}
		}
		
		// Clear callback after updating state
		SetZapdProgressCallback(nullptr);
		
		// Add log message after releasing the mutex to avoid deadlock
		AddLog(completionMessage);

		if (progressThread.joinable()) {
			progressThread.join();
		}
	}

	bool BootSelect(void* wnd, int w, int h)
	{
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

		SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

		SDL_Window* window = SDL_CreateWindow("2Ship2Harkinian - Boot Menu", 
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_RESIZABLE);
		SDL_ShowWindow(window);

		if (!dx11glue::CreateDeviceD3D(wnd, w, h))
		{
			dx11glue::CleanupDeviceD3D();
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 10.0f;
		style.ChildRounding = 10.0f;
		style.FrameRounding = 8.0f;
		style.GrabRounding = 8.0f;
		style.TabRounding = 6.0f;
		style.TabBorderSize = 0.0f;
		style.ScrollbarRounding = 8.0f;
		style.WindowPadding = ImVec2(20.0f, 20.0f);
		style.FramePadding = ImVec2(16.0f, 10.0f);
		style.ItemSpacing = ImVec2(12.0f, 10.0f);
		style.ItemInnerSpacing = ImVec2(10.0f, 8.0f);
		style.IndentSpacing = 25.0f;
		style.ScrollbarSize = 16.0f;
		style.GrabMinSize = 12.0f;
		style.WindowBorderSize = 0.0f;
		style.FrameBorderSize = 0.0f;
		style.PopupBorderSize = 1.0f;
		
		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.96f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
		colors[ImGuiCol_Button] = ImVec4(0.20f, 0.50f, 0.85f, 0.60f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.55f, 0.90f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.45f, 0.80f, 1.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
		colors[ImGuiCol_Border] = ImVec4(0.25f, 0.55f, 0.90f, 0.60f);
		colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.35f, 0.50f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.35f, 0.40f, 0.70f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.40f, 0.45f, 0.90f);
		colors[ImGuiCol_Header] = ImVec4(0.20f, 0.50f, 0.85f, 0.40f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.55f, 0.90f, 0.70f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.45f, 0.80f, 1.00f);
		colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.20f, 0.50f, 0.85f, 1.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
		
		ImGui_ImplSDL2_InitForD3D(window);
		ImGui_ImplDX11_Init(dx11glue::g_pd3dDevice, dx11glue::g_pd3dDeviceContext);

		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize.x = static_cast<float>(w);
		io.DisplaySize.y = static_cast<float>(h);

		ImFontConfig fontCfg;
		fontCfg.OversampleH = 1;
		fontCfg.OversampleV = 1;
		fontCfg.PixelSnapH = true;
		fontCfg.SizePixels = 16.0f;
		
		std::string installPath = GetAppBundlePath();
		std::string fontPath = installPath + "/assets/custom/fonts/Inconsolata-Regular.ttf";
		ImFont* font = nullptr;
		
		if (std::filesystem::exists(fontPath)) {
			font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f, &fontCfg);
		}
		
		// Fallback to default font if Inconsolata not found
		if (!font) {
			font = io.Fonts->AddFontDefault(&fontCfg);
		}
		
		io.FontDefault = font;
		io.FontGlobalScale = (std::max)(1.0f, static_cast<float>(h) / 900.0f);
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;

		// Check if storage location is configured
		// Try to read the saved preference, but don't crash if ApplicationData isn't available
		bool hasStorageConfig = false;
		try {
			auto appData = winrt::Windows::Storage::ApplicationData::Current();
			if (appData) {
				auto localSettings = appData.LocalSettings();
				if (localSettings) {
					auto container = localSettings.Containers().TryLookup(L"Settings");
					if (container) {
						auto value = container.Values().TryLookup(L"StorageLocation");
						hasStorageConfig = (value != nullptr);
					}
				}
			}
		} catch (const winrt::hresult_error&) {
			// ApplicationData access failed - this can happen if called too early
			// Default to showing setup screen to be safe
			hasStorageConfig = false;
		} catch (...) {
			// Any other exception - default to showing setup screen
			hasStorageConfig = false;
		}

		if (!hasStorageConfig) {
			g_extractionState.state = BootState::Setup;
		} else {
			// Check if mm.o2r already exists
			auto auxRoot = GetAuxRoot();
			std::filesystem::create_directories(auxRoot);
			const std::filesystem::path mmO2rPath = auxRoot / "mm.o2r";
			
			if (std::filesystem::exists(mmO2rPath))
			{
				g_extractionState.state = BootState::Ready;
			}
			else
			{
				g_extractionState.state = BootState::SelectingROM;
			}
		}

		std::thread extractionThread;
		bool extractionThreadStarted = false;

		bool running = true;
		bool shouldContinue = false;

		while (running)
		{
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				ImGui_ImplSDL2_ProcessEvent(&event);
				if (event.type == SDL_QUIT)
				{
					running = false;
				}
			}

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplSDL2_NewFrame();
			io.DisplaySize.x = static_cast<float>(w);
			io.DisplaySize.y = static_cast<float>(h);

			ImGui::NewFrame();

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w), static_cast<float>(h)), ImGuiCond_FirstUseEver);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			{
				ImGui::Begin("Boot Menu", 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

				const float contentWidth = 900.0f;
				const float contentHeight = 650.0f;
				float startY = (h - contentHeight) * 0.5f;
				ImGui::SetCursorPosY(startY);

				ImGui::SetCursorPosX((w - contentWidth) * 0.5f);
				ImGui::BeginChild("Content", ImVec2(contentWidth, contentHeight), true, ImGuiWindowFlags_None);

				// Get current state to check if extraction is in progress
				BootState currentStateForTabs;
				{
					std::lock_guard<std::mutex> lock(g_extractionState.mutex);
					currentStateForTabs = g_extractionState.state;
				}
				
				static int selectedTab = 0;
				bool isExtracting = (currentStateForTabs == BootState::Extracting);
				
				// Force Main tab during extraction
				if (isExtracting) {
					selectedTab = 0;
				}
				
				ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 1.0f);
				ImGuiTabBarFlags tabBarFlags = ImGuiTabBarFlags_NoTabListScrollingButtons | ImGuiTabBarFlags_FittingPolicyResizeDown;
				
				if (ImGui::BeginTabBar("MainTabs", tabBarFlags))
				{
					if (isExtracting) {
						ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
					}
					
					bool mainTabActive = ImGui::BeginTabItem("Main");
					if (mainTabActive) {
						if (!isExtracting) {
							selectedTab = 0;
						}
						ImGui::EndTabItem();
					}
					
					bool aboutTabActive = ImGui::BeginTabItem("About");
					if (aboutTabActive) {
						if (!isExtracting) {
							selectedTab = 1;
						} else {
							selectedTab = 0;
						}
						ImGui::EndTabItem();
					}
					
					if (isExtracting) {
						ImGui::PopStyleVar();
					}
					
					ImGui::EndTabBar();
				}
				ImGui::PopStyleVar();

				ImGui::Spacing();

				if (selectedTab == 0)
				{
					BootState currentState;
					std::string errorMsg;
					float progressPercent = 0.0f;
					{
						std::lock_guard<std::mutex> lock(g_extractionState.mutex);
						currentState = g_extractionState.state;
						errorMsg = g_extractionState.errorMessage;
						progressPercent = g_extractionState.progressPercent;
					}

					ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize("2Ship2Harkinian").x) * 0.5f);
					ImGui::Text("2Ship2Harkinian");
					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Spacing();

					switch (currentState)
					{
					case BootState::Setup:
					{
						ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize("Storage Location Setup").x) * 0.5f);
						ImGui::Text("Storage Location Setup");
						ImGui::Spacing();
						ImGui::Separator();
						ImGui::Spacing();
						
						ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize("Choose where game files will be stored:").x) * 0.5f);
						ImGui::TextWrapped("Choose where game files will be stored:");
						ImGui::Spacing();
						ImGui::Spacing();
						
						static int selectedLocation = static_cast<int>(StorageLocation::DDrive);
						
						const float radioWidth = 400.0f;
						ImGui::SetCursorPosX((contentWidth - radioWidth) * 0.5f);
						
						ImGui::RadioButton("LocalState (App Data Folder)", &selectedLocation, static_cast<int>(StorageLocation::LocalState));
						ImGui::SetCursorPosX((contentWidth - radioWidth) * 0.5f);
						ImGui::TextWrapped("  Stores files in the app's local data folder");
						ImGui::Spacing();
						
						ImGui::SetCursorPosX((contentWidth - radioWidth) * 0.5f);
						ImGui::RadioButton("D:\\2ship\\ (Internal Drive)", &selectedLocation, static_cast<int>(StorageLocation::DDrive));
						ImGui::SetCursorPosX((contentWidth - radioWidth) * 0.5f);
						ImGui::TextWrapped("  Stores files on D: drive (recommended for internal storage)");
						ImGui::Spacing();
						
						ImGui::SetCursorPosX((contentWidth - radioWidth) * 0.5f);
						ImGui::RadioButton("E:\\2ship\\ (USB/External Drive)", &selectedLocation, static_cast<int>(StorageLocation::EDrive));
						ImGui::SetCursorPosX((contentWidth - radioWidth) * 0.5f);
						ImGui::TextWrapped("  Stores files on E: drive (for USB/external storage)");
						ImGui::Spacing();
						ImGui::Spacing();
						
						const float buttonWidth = 200.0f;
						const float buttonHeight = 45.0f;
						ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
						if (ImGui::Button("Continue", ImVec2(buttonWidth, buttonHeight))) {
							SaveStorageLocation(static_cast<StorageLocation>(selectedLocation));
							// Now check for mm.o2r
							auto auxRoot = GetAuxRoot();
							std::filesystem::create_directories(auxRoot);
							const std::filesystem::path mmO2rPath = auxRoot / "mm.o2r";
							
							if (std::filesystem::exists(mmO2rPath)) {
								g_extractionState.state = BootState::Ready;
							} else {
								g_extractionState.state = BootState::SelectingROM;
							}
						}
						break;
					}
					
					case BootState::CheckingO2R:
						ImGui::Text("Checking for game assets...");
						break;

				case BootState::SelectingROM:
				{
					ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize("Game assets not found. Please select a ROM file to extract assets.").x) * 0.5f);
					ImGui::TextWrapped("Game assets not found. Please select a ROM file to extract assets.");
					ImGui::Spacing();
					ImGui::Spacing();
					
					const float buttonWidth = 220.0f;
					const float buttonHeight = 45.0f;
					ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
					if (ImGui::Button("Select ROM File", ImVec2(buttonWidth, buttonHeight)))
					{
						char romBuffer[1024] = { 0 };
						if (uwp_pick_rom(romBuffer, sizeof(romBuffer)))
						{
							std::string romPath(romBuffer);
							if (std::filesystem::exists(romPath))
							{
								{
									std::lock_guard<std::mutex> lock(g_extractionState.mutex);
									g_extractionState.selectedRomPath = romPath;
								}

								const std::string installPath = GetAppBundlePath();
								const std::string assetsPath = installPath + "/assets";
								if (!std::filesystem::exists(assetsPath))
								{
									{
										std::lock_guard<std::mutex> lock(g_extractionState.mutex);
										g_extractionState.state = BootState::ExtractionFailed;
										g_extractionState.errorMessage = "Missing assets folder needed to generate O2R file. Please reinstall or add the assets folder.";
									}
								}
								else
								{
									// Start extraction in background thread
									if (!extractionThreadStarted)
									{
										auto auxRoot = GetAuxRoot();
										extractionThread = std::thread(ExtractionThreadWorker, romPath, installPath, auxRoot.string());
										extractionThreadStarted = true;
									}
								}
							}
							else
							{
								{
									std::lock_guard<std::mutex> lock(g_extractionState.mutex);
									g_extractionState.state = BootState::ExtractionFailed;
									g_extractionState.errorMessage = "Selected ROM file does not exist: " + romPath;
								}
							}
						}
						else
						{
							{
								std::lock_guard<std::mutex> lock(g_extractionState.mutex);
								g_extractionState.state = BootState::ExtractionFailed;
								g_extractionState.errorMessage = "No ROM was selected.";
							}
						}
					}
					break;
				}

				case BootState::Extracting:
				{
					ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize("Extracting game assets from ROM...").x) * 0.5f);
					ImGui::TextWrapped("Extracting game assets from ROM...");
					ImGui::Spacing();
					ImGui::Spacing();
					
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.26f, 0.59f, 0.98f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
					const float progressBarWidth = 550.0f;
					ImGui::SetCursorPosX((contentWidth - progressBarWidth) * 0.5f);
					ImGui::ProgressBar(progressPercent / 100.0f, ImVec2(progressBarWidth, 35.0f), "");
					ImGui::PopStyleColor(2);
					
					ImGui::Spacing();
					
					char progressText[32];
					snprintf(progressText, sizeof(progressText), "%.0f%%", progressPercent);
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
					ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize(progressText).x) * 0.5f);
					ImGui::Text("%s", progressText);
					ImGui::PopStyleColor();
					
					ImGui::Spacing();
					ImGui::Spacing();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.87f, 1.0f));
					ImGui::Text("Details:");
					ImGui::PopStyleColor();
					ImGui::Spacing();
					ImGuiWindowFlags logFlags = ImGuiWindowFlags_None;
					if (currentState == BootState::Extracting) {
						logFlags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
					}
					ImGui::BeginChild("Log", ImVec2(0, 280), true, logFlags);
					{
						std::vector<std::string> logLinesCopy;
						{
							std::lock_guard<std::mutex> lock(g_extractionState.mutex);
							logLinesCopy = g_extractionState.logLines;
						}
						
						if (logLinesCopy.empty()) {
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.65f, 1.0f));
							ImGui::TextWrapped("Waiting for extraction output...");
							ImGui::PopStyleColor();
						} else {
							for (size_t i = 0; i < logLinesCopy.size(); ++i) {
								const auto& line = logLinesCopy[i];
								if (line.empty()) continue;
								
								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.90f, 1.0f));
								ImGui::TextUnformatted(line.c_str());
								ImGui::PopStyleColor();
							}
							
							if (currentState == BootState::Extracting && !logLinesCopy.empty()) {
								ImGui::SetScrollHereY(1.0f);
							}
						}
					}
					ImGui::EndChild();
					break;
				}

				case BootState::ExtractionComplete:
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
					ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize("Asset extraction completed successfully!").x) * 0.5f);
					ImGui::TextWrapped("Asset extraction completed successfully!");
					ImGui::PopStyleColor();
					ImGui::Spacing();
					ImGui::Spacing();
					
					const float buttonWidth = 220.0f;
					const float buttonHeight = 45.0f;
					ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
					
					if (ImGui::Button("Continue to Game", ImVec2(buttonWidth, buttonHeight)))
					{
						shouldContinue = true;
						running = false;
					}
					break;
				}

				case BootState::ExtractionFailed:
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
					ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize("Extraction Failed").x) * 0.5f);
					ImGui::Text("Extraction Failed");
					ImGui::PopStyleColor();
					ImGui::Spacing();
					
					if (!errorMsg.empty())
					{
						ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize(errorMsg.c_str()).x) * 0.5f);
						ImGui::TextWrapped("%s", errorMsg.c_str());
					}
					
					ImGui::Spacing();
					ImGui::Spacing();
					
					const float buttonWidth2 = 160.0f;
					const float buttonHeight2 = 40.0f;
					ImGui::SetCursorPosX((contentWidth - (buttonWidth2 * 2 + 20.0f)) * 0.5f);
					
					if (ImGui::Button("Try Again", ImVec2(buttonWidth2, buttonHeight2)))
					{
						{
							std::lock_guard<std::mutex> lock(g_extractionState.mutex);
							g_extractionState.state = BootState::SelectingROM;
							g_extractionState.errorMessage.clear();
						}
						extractionThreadStarted = false;
					}
					
					ImGui::SameLine();
					ImGui::Spacing();
					ImGui::SameLine();
					
					if (ImGui::Button("Exit", ImVec2(buttonWidth2, buttonHeight2)))
					{
						running = false;
					}
					break;
				}

				case BootState::Ready:
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
					ImGui::SetCursorPosX((contentWidth - ImGui::CalcTextSize("Game assets found. Ready to launch!").x) * 0.5f);
					ImGui::TextWrapped("Game assets found. Ready to launch!");
					ImGui::PopStyleColor();
					ImGui::Spacing();
					ImGui::Spacing();
					
					const float buttonWidth3 = 220.0f;
					const float buttonHeight3 = 45.0f;
					ImGui::SetCursorPosX((contentWidth - buttonWidth3) * 0.5f);
					if (ImGui::Button("Launch Game", ImVec2(buttonWidth3, buttonHeight3)))
					{
						shouldContinue = true;
						running = false;
					}
					break;
				}
				}
				}
				else if (selectedTab == 1)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.98f, 1.0f, 1.0f));
					float titleWidth2 = ImGui::CalcTextSize("2Ship2Harkinian").x;
					ImGui::SetCursorPosX((contentWidth - titleWidth2) * 0.5f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15.0f);
					ImGui::Text("2Ship2Harkinian");
					ImGui::PopStyleColor();
					ImGui::Spacing();
					ImGui::Spacing();
					
					ImGui::TextWrapped("A decompilation and port of The Legend of Zelda: Majora's Mask.");
					ImGui::Spacing();
					ImGui::Spacing();
					
					static std::string cachedVersionStr;
					static bool versionCached = false;
					
					if (!versionCached) {
						try {
							auto package = winrt::Windows::ApplicationModel::Package::Current();
							if (package) {
								auto version = package.Id().Version();
								char versionStr[64];
								snprintf(versionStr, sizeof(versionStr), "Version: %d.%d.%d.%d", 
									version.Major, version.Minor, version.Build, version.Revision);
								cachedVersionStr = versionStr;
								versionCached = true;
							}
						} catch (...) {
							// Version not available use fallback
							cachedVersionStr = "Version: Unknown";
							versionCached = true;
						}
					}
					
					if (!cachedVersionStr.empty()) {
						ImGui::Text("%s", cachedVersionStr.c_str());
					}
					ImGui::Spacing();
					ImGui::Spacing();
					
					ImGui::TextWrapped("This is an unofficial Universal Windows Platform (UWP) port of 2Ship2Harkinian maintained by SternXD and originally ported by worleydl.");
					ImGui::Spacing();
					ImGui::Spacing();
					
					ImGui::TextWrapped("Original project:");
					ImGui::Text("https://github.com/HarbourMasters/2ship2harkinian");
					ImGui::Text("https://2ship.equipment/");
					ImGui::Spacing();
					ImGui::Spacing();
					
					ImGui::TextWrapped("2Ship2Harkinian source code is released under CC0-1.0.");
					ImGui::TextWrapped("This software includes no copyrighted game assets.");
					ImGui::Spacing();
					ImGui::Spacing();
					
					ImGui::TextWrapped("A legally obtained copy of The Legend of Zelda: Majora's Mask is required.");
					ImGui::Spacing();
					ImGui::Spacing();
					
					ImGui::TextWrapped("2Ship2Harkinian and its logos are associated with the original HarbourMasters project.");
					ImGui::TextWrapped("No endorsement is implied.");
				}

				ImGui::EndChild();
				ImGui::End();
			}

			ImGui::PopStyleVar(1);

			ImGui::Render();

			const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
			dx11glue::g_pd3dDeviceContext->OMSetRenderTargets(1, &dx11glue::g_mainRenderTargetView, nullptr);
			dx11glue::g_pd3dDeviceContext->ClearRenderTargetView(dx11glue::g_mainRenderTargetView, clear_color_with_alpha);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

			dx11glue::g_pSwapChain->Present(1, 0);
			uwp_ProcessEvents();
		}

		// Wait for extraction thread if it's running
		if (extractionThreadStarted && extractionThread.joinable())
		{
			extractionThread.join();
		}

		ImGui_ImplDX11_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();

		dx11glue::CleanupDeviceD3D();
		SDL_DestroyWindow(window);
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);

		return shouldContinue;
	}
} // namespace bootmenu