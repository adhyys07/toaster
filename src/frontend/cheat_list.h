#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace emu::frontend {

// One memory patch. `compare` is set for codes that only take effect when the
// address already holds a particular value, which is how Game Genie codes
// patch a specific bank rather than every bank mapped at that address.
struct Cheat {
    std::string description;
    uint16_t address = 0;
    uint8_t value = 0;
    std::optional<uint8_t> compare;
    bool enabled = true;
};

// Decodes a 6- or 8-character NES Game Genie code. Returns nullopt for a code
// of the wrong length or with a character outside the Game Genie alphabet.
std::optional<Cheat> decodeGameGenie(const std::string& code);

// Reads "<game>.cht" beside the ROM. Each line is either a Game Genie code or
// a raw patch as AAAA:VV or AAAA?CC:VV, optionally followed by a description.
// '#' starts a comment and blank lines are skipped.
std::vector<Cheat> loadCheatFile(const std::string& game_path);

bool saveCheatFile(const std::string& game_path,
                   const std::vector<Cheat>& cheats);

// Applies the enabled cheats to a byte read from `address`. Returns the value
// unchanged when nothing matches.
uint8_t applyCheats(const std::vector<Cheat>& cheats,
                    uint16_t address,
                    uint8_t value);

} // namespace emu::frontend
