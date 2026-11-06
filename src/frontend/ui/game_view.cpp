#include "game_view.h"
#include <cctype>
#include <filesystem>

namespace emu::frontend::ui {
std::string foldCase(const std::string& text) {
    std::string folded;
    folded.reserve(text.size());
    for (const unsigned char ch : text) {
        folded.push_back(static_cast<char>(std::tolower(ch)));
    }
    return folded;
}

bool fuzzyMatch(const std::string& haystack, const std::string& needle, int* score) {
    if (score) *score = 0;
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;

    std::size_t position = 0;
    std::size_t previous = std::string::npos;
    int total = 0;

    for (const char wanted : needle) {
        while (position < haystack.size() && haystack[position] != wanted) {
            ++position;
        }
        if (position == haystack.size()) return false;

        const bool consecutive =
            previous != std::string::npos && position == previous + 1;
        const bool word_start =
            position == 0 || haystack[position - 1] == ' ' ||
            haystack[position - 1] == '-' || haystack[position - 1] == '_' ||
            haystack[position - 1] == '(';

        total += 10 + (consecutive ? 12 : 0) + (word_start ? 15 : 0);
        previous = position;
        ++position;
    }

    total -= static_cast<int>(haystack.size() / 8);
    if (score) *score = total;
    return true;    
}

SystemId systemFromPath(const std::string& path) {
    const std::string extension =
        foldCase(std::filesystem::path(path).extension().string());

    if (extension == ".nes") return SystemId::Nes;
    if (extension == ".gb" || extension == ".gbc") return SystemId::GameBoy;
    if (extension == ".ch8") return SystemId::Chip8;
    if (extension == ".cue" || extension == ".bin" || extension == ".chd" ||
        extension == ".pbp" || extension == ".m3u") {
        return SystemId::Ps1;
    }
    if (extension == ".cso") return SystemId::Psp;
    if (extension == ".iso") return SystemId::Psp;
    return SystemId::Unknown;
}

const char* systemShortName(SystemId system) {
    switch (system) {
    case SystemId::Nes: return "NES";
    case SystemId::GameBoy: return "GB";
    case SystemId::Chip8: return "CH8";
    case SystemId::Ps1: return "PS1";
    case SystemId::Psp: return "PSP";
    case SystemId::Ps2: return "PS2";
    case SystemId::Unknown:
    default: return "?";
    }
}

}