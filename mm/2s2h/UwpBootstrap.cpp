#ifdef _UWP

#include "UwpBootstrap.h"

#include "SDL2/SDL_messagebox.h"
#include <filesystem>
#include <Windows.h>
#include <mutex>

#include "Extractor/Extract.h"
#include <ship/Context.h>

extern "C" __declspec(dllimport) bool uwp_pick_rom(char* outPath, size_t outLen);

namespace {
std::filesystem::path GetAuxRoot() {
    return std::filesystem::path(Ship::Context::GetPathRelativeToAuxiliary(""));
}
}

bool EnsureO2rPresentUwp(const std::string& appShortName) {
    
    auto auxRoot = GetAuxRoot();
    std::filesystem::create_directories(auxRoot);

    const std::filesystem::path mmO2rPath = auxRoot / "mm.o2r";
    if (std::filesystem::exists(mmO2rPath)) {
        // Verify the file is valid
        const auto o2rSize = std::filesystem::file_size(mmO2rPath);
        if (o2rSize >= 1024 * 1024) { // At least 1MB
            return true;
        }
    }

    // If we get here, the boot menu should have been called from the UWP entry point
    // before SDL_main was invoked. If the file still doesn't exist, something went really wrong.
    if (!std::filesystem::exists(mmO2rPath)) {
        Extractor::ShowErrorBox("No O2R File", "mm.o2r file not found. Please use the boot menu to extract assets.");
        return false;
    }

    const auto o2rSize = std::filesystem::file_size(mmO2rPath);
    if (o2rSize < 1024 * 1024) { // Less than 1MB is suspicious
        Extractor::ShowErrorBox("Generation failed", 
            ("Generated mm.o2r file is too small (" + std::to_string(o2rSize) + " bytes). Please try again.").c_str());
        return false;
    }
    
    return true;
}

#endif

