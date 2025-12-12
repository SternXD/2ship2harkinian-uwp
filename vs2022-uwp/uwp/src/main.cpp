#include <Windows.h>
#include "SDL2/SDL.h"
#include "bootmenu.h"
#include <filesystem>
#include <string>
#include <map>

extern "C" __declspec(dllimport) void* uwp_GetWindowReference();
extern "C" __declspec(dllimport) const char* uwp_get_aux_root();

namespace {
	// Cache drive access state to avoid repeated checks
	std::map<std::string, bool> g_driveAccessCache;
	
	// Check if a drive is accessible using UWP APIs
	bool CheckDriveAccess(const std::string& driveName) {
		// Check cache first
		auto it = g_driveAccessCache.find(driveName);
		if (it != g_driveAccessCache.end()) {
			return it->second;
		}
		
		bool accessible = false;
		try {
			std::wstring drivePath = std::wstring(driveName.begin(), driveName.end()) + L"\\*.*";
			
			WIN32_FIND_DATA findData;
			HANDLE searchHandle = FindFirstFileExFromAppW(
				drivePath.c_str(),
				FindExInfoBasic,
				&findData,
				FindExSearchNameMatch,
				nullptr,
				0
			);
			
			if (searchHandle != INVALID_HANDLE_VALUE && searchHandle != nullptr) {
				FindClose(searchHandle);
				accessible = true;
			}
		} catch (...) {
			// Drive not accessible
			accessible = false;
		}
		
		// Cache the result
		g_driveAccessCache[driveName] = accessible;
		return accessible;
	}
	
	std::filesystem::path GetAuxRoot() {
		// Try to find a suitable drive (prefer D:\, then E:\)
		const char* drives[] = { "D:", "E:" };
		for (const char* drive : drives) {
			if (CheckDriveAccess(drive)) {
				return std::filesystem::path(std::string(drive) + "/2ship/");
			}
		}
		
		// Final fallback to D:\2ship\ (most common for internal drives)
		return std::filesystem::path("D:/2ship/");
	}
}

int bootstrap(int argc, char** argv)
{
	uwp_GetWindowReference(); // Call once to init reference for other threads

	// Check if mm.o2r exists before starting the game
	auto auxRoot = GetAuxRoot();
	std::filesystem::create_directories(auxRoot);
	const std::filesystem::path mmO2rPath = auxRoot / "mm.o2r";
	
	// Check if file exists and is valid (at least 1MB)
	bool o2rExists = false;
	if (std::filesystem::exists(mmO2rPath)) {
		try {
			const auto o2rSize = std::filesystem::file_size(mmO2rPath);
			if (o2rSize >= 1024 * 1024) { // At least 1MB
				o2rExists = true;
			}
		} catch (...) {
			// File size check failed, treat as not existing
		}
	}
	
	if (!o2rExists) {
		void* windowHandle = uwp_GetWindowReference();
		if (windowHandle != nullptr) {
			int windowWidth = 1920;
			int windowHeight = 1080;
			
			// Show boot menu, this will handle ROM selection and extraction
			bool shouldContinue = bootmenu::BootSelect(windowHandle, windowWidth, windowHeight);
			
			if (!shouldContinue) {
				// User cancelled or extraction failed, exit
				return 1;
			}
			
			// Verify mm.o2r was created and is valid, if not, exit
			if (!std::filesystem::exists(mmO2rPath)) {
				return 1;
			}
			try {
				const auto o2rSize = std::filesystem::file_size(mmO2rPath);
				if (o2rSize < 1024 * 1024) {
					return 1;
				}
			} catch (...) {
				return 1;
			}
		} else {
			// Can't show boot menu without window handle, exit
			return 1;
		}
	}

	return SDL_main(argc, argv);
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR argv, int argc)
{
	return SDL_WinRTRunApp(bootstrap, NULL);
}
