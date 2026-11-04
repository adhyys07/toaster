#include "ui_font.h"

#include <SDL.h>
#include <filesystem>
#include <vector>

namespace emu::frontend {
namespace fs = std::filesystem;

TTF_Font* openUiFont(int point_size, std::string& error) {
    std::vector<fs::path> candidates;

    if (char* base = SDL_GetBasePath()){
        const fs::path root(base);
        SDL_free(base);
         candidates.push_back(root / "assets" / "fonts" / "Inter-Regular.ttf");
        candidates.push_back(root / "assets" / "fonts" / "DejaVuSans.ttf");
    }
    candidates.push_back(fs::path("assets/fonts/Inter-Regular.ttf"));
    candidates.push_back(fs::path("assets/fonts/DejaVuSans.ttf"));

#ifdef _WIN32
    candidates.push_back("C:/Windows/Fonts/segoeui.ttf");
    candidates.push_back("C:/Windows/Fonts/arial.ttf");
    candidates.push_back("C:/Windows/Fonts/tahoma.ttf");
#elif defined(__APPLE__)
    candidates.push_back("/System/Library/Fonts/SFNS.ttf");
    candidates.push_back("/System/Library/Fonts/Helvetica.ttc");
    candidates.push_back("/Library/Fonts/Arial.ttf");
#else
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    candidates.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/noto/NotoSans-Regular.ttf");
    candidates.push_back("/usr/share/fonts/gnu-free/FreeSans.ttf");
#endif
    
    for (const fs::path& candidate : candidates) {
        std::error_code ignored;
        if (!fs::is_regular_file(candidate, ignored)) continue;
        if (TTF_Font* font = TTF_OpenFont(candidate.string().c_str(), point_size))
            return font;
    }
    error = "No usable UI font found. Place a .ttf in assets/fonts/ "
            "next to the executable.";
    return nullptr;
}
}