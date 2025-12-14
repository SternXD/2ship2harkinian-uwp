#include <Windows.h>
#include "SDL2/SDL.h"
#include "bootmenu.h"
#include <filesystem>
#include <string>
#include <map>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>

extern "C" __declspec(dllimport) void* uwp_GetWindowReference();
extern "C" __declspec(dllimport) const char* uwp_get_aux_root();

namespace {
	enum class StorageLocation
	{
		LocalState,
		DDrive,
		EDrive
	};
	
	StorageLocation GetStorageLocation() {
		try {
			auto appData = winrt::Windows::Storage::ApplicationData::Current();
			if (!appData) {
				return StorageLocation::DDrive;
			}
			auto localSettings = appData.LocalSettings();
			if (!localSettings) {
				return StorageLocation::DDrive;
			}
			auto container = localSettings.Containers().TryLookup(L"Settings");
			if (container) {
				auto value = container.Values().TryLookup(L"StorageLocation");
				if (value) {
					int location = value.as<int>();
					return static_cast<StorageLocation>(location);
				}
			}
		} catch (...) {
			// Settings not available, use default
		}
		return StorageLocation::DDrive; // Default to D: drive
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
			
			auxRoot = GetAuxRoot();
			const std::filesystem::path mmO2rPathAfterBoot = auxRoot / "mm.o2r";
			
			// Verify mm.o2r was created and is valid, if not, exit
			if (!std::filesystem::exists(mmO2rPathAfterBoot)) {
				return 1;
			}
			try {
				const auto o2rSize = std::filesystem::file_size(mmO2rPathAfterBoot);
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
