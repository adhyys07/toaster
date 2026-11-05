#include "cheat_list.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace emu::frontend {
namespace {

// The Game Genie alphabet. A letter's index supplies four bits of the code.
constexpr char kAlphabet[] = "APZLGITYEOXUKSVN";

std::filesystem::path cheatPath(const std::string& game_path) {
    return std::filesystem::path(game_path).replace_extension(".cht");
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

bool parseHex(const std::string& text, unsigned& out) {
    if (text.empty() || text.size() > 4) return false;
    unsigned value = 0;
    for (char ch : text) {
        const char upper =
            static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (upper >= '0' && upper <= '9') {
            value = value * 16 + static_cast<unsigned>(upper - '0');
        } else if (upper >= 'A' && upper <= 'F') {
            value = value * 16 + static_cast<unsigned>(upper - 'A' + 10);
        } else {
            return false;
        }
    }
    out = value;
    return true;
}

} // namespace

std::optional<Cheat> decodeGameGenie(const std::string& code) {
    if (code.size() != 6 && code.size() != 8) return std::nullopt;

    int digit[8] = {};
    for (size_t i = 0; i < code.size(); ++i) {
        const char upper =
            static_cast<char>(std::toupper(static_cast<unsigned char>(code[i])));
        const char* found = std::strchr(kAlphabet, upper);
        if (!found || upper == '\0') return std::nullopt;
        digit[i] = static_cast<int>(found - kAlphabet);
    }

    Cheat cheat;
    cheat.description = code;

    // Each letter carries four bits, scattered across the code. This is the
    // published Game Genie bit assignment; the address is relative to 0x8000.
    const int address =
        ((digit[3] & 7) << 12) | ((digit[5] & 7) << 8) | ((digit[4] & 8) << 8) |
        ((digit[2] & 7) << 4) | ((digit[1] & 8) << 4) | (digit[4] & 7) |
        (digit[3] & 8);
    cheat.address = static_cast<uint16_t>(0x8000 + address);

    int value = ((digit[1] & 7) << 4) | ((digit[0] & 8) << 4) | (digit[0] & 7);

    if (code.size() == 8) {
        value |= digit[7] & 8;
        const int compare = ((digit[7] & 7) << 4) | ((digit[6] & 8) << 4) |
                            (digit[6] & 7) | (digit[5] & 8);
        cheat.compare = static_cast<uint8_t>(compare);
    } else {
        value |= digit[5] & 8;
    }

    cheat.value = static_cast<uint8_t>(value);
    return cheat;
}

std::vector<Cheat> loadCheatFile(const std::string& game_path) {
    std::vector<Cheat> cheats;

    std::ifstream file(cheatPath(game_path));
    if (!file) return cheats;

    std::string line;
    while (std::getline(file, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);

        line = trim(line);
        if (line.empty()) continue;

        // A leading '-' disables a cheat without removing it from the file.
        bool enabled = true;
        if (line.front() == '-') {
            enabled = false;
            line = trim(line.substr(1));
            if (line.empty()) continue;
        }

        // Split off an optional trailing description.
        std::string token = line;
        std::string description;
        const auto space = line.find_first_of(" \t");
        if (space != std::string::npos) {
            token = line.substr(0, space);
            description = trim(line.substr(space + 1));
        }

        Cheat cheat;
        const auto colon = token.find(':');
        if (colon == std::string::npos) {
            // No colon: this is a Game Genie code.
            const auto decoded = decodeGameGenie(token);
            if (!decoded) {
                std::fprintf(stderr, "Ignoring unreadable cheat code '%s'\n",
                             token.c_str());
                continue;
            }
            cheat = *decoded;
        } else {
            // AAAA:VV, or AAAA?CC:VV for a compare.
            std::string address_part = token.substr(0, colon);
            const std::string value_part = token.substr(colon + 1);

            std::optional<unsigned> compare;
            const auto question = address_part.find('?');
            if (question != std::string::npos) {
                unsigned parsed = 0;
                if (!parseHex(address_part.substr(question + 1), parsed)) {
                    std::fprintf(stderr, "Ignoring cheat '%s': bad compare\n",
                                 token.c_str());
                    continue;
                }
                compare = parsed;
                address_part = address_part.substr(0, question);
            }

            unsigned address = 0;
            unsigned value = 0;
            if (!parseHex(address_part, address) ||
                !parseHex(value_part, value) || value > 0xFF) {
                std::fprintf(stderr, "Ignoring cheat '%s': bad address or value\n",
                             token.c_str());
                continue;
            }

            cheat.address = static_cast<uint16_t>(address);
            cheat.value = static_cast<uint8_t>(value);
            if (compare) cheat.compare = static_cast<uint8_t>(*compare & 0xFF);
            cheat.description = token;
        }

        if (!description.empty()) cheat.description = description;
        cheat.enabled = enabled;
        cheats.push_back(std::move(cheat));
    }

    return cheats;
}

bool saveCheatFile(const std::string& game_path,
                   const std::vector<Cheat>& cheats) {
    std::ofstream file(cheatPath(game_path), std::ios::trunc);
    if (!file) return false;

    file << "# Toaster cheat list. One per line:\n"
         << "#   GXUZLX            a Game Genie code\n"
         << "#   00FF:63           write 0x63 at 0x00FF\n"
         << "#   8123?A9:EA        only patch when the byte reads 0xA9\n"
         << "# A leading '-' disables a line.\n";

    for (const Cheat& cheat : cheats) {
        if (!cheat.enabled) file << '-';
        char encoded[32] = {};
        if (cheat.compare) {
            std::snprintf(encoded, sizeof(encoded), "%04X?%02X:%02X",
                          cheat.address, *cheat.compare, cheat.value);
        } else {
            std::snprintf(encoded, sizeof(encoded), "%04X:%02X",
                          cheat.address, cheat.value);
        }
        file << encoded;
        if (!cheat.description.empty()) file << ' ' << cheat.description;
        file << '\n';
    }

    return file.good();
}

uint8_t applyCheats(const std::vector<Cheat>& cheats,
                    uint16_t address,
                    uint8_t value) {
    for (const Cheat& cheat : cheats) {
        if (!cheat.enabled || cheat.address != address) continue;
        // A compare cheat only fires when the byte already matches, so the
        // patch lands on one bank rather than whichever is mapped.
        if (cheat.compare && *cheat.compare != value) continue;
        return cheat.value;
    }
    return value;
}

} // namespace emu::frontend
