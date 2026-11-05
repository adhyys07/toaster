#include "cheat_list.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

void group(const char* name) {
    std::printf("\n%s\n", name);
}

} // namespace

int main() {
    using emu::frontend::Cheat;

    std::printf("cheats\n");

    group("Game Genie decoding");
    {
        // Every letter is index 0, so every bit field is zero: the address is
        // the base of cartridge space and the value is zero.
        const auto all_zero = emu::frontend::decodeGameGenie("AAAAAA");
        check(all_zero.has_value(), "a six-character code decodes");
        if (all_zero) {
            check(all_zero->address == 0x8000, "all-A addresses 0x8000");
            check(all_zero->value == 0x00, "all-A patches with zero");
            check(!all_zero->compare.has_value(),
                  "a six-character code has no compare value");
        }

        const auto eight = emu::frontend::decodeGameGenie("AAAAAAAA");
        check(eight.has_value(), "an eight-character code decodes");
        if (eight) {
            check(eight->compare.has_value(),
                  "an eight-character code carries a compare value");
        }

        // The decoder must stay inside cartridge space whatever the input.
        const auto high = emu::frontend::decodeGameGenie("NNNNNN");
        check(high.has_value(), "an all-N code decodes");
        if (high) {
            check(high->address >= 0x8000,
                  "a decoded address lands in cartridge space");
        }

        check(!emu::frontend::decodeGameGenie("ABC").has_value(),
              "a code of the wrong length is rejected");
        check(!emu::frontend::decodeGameGenie("AAAAAAA").has_value(),
              "a seven-character code is rejected");
        check(!emu::frontend::decodeGameGenie("AAAABQ").has_value(),
              "a character outside the alphabet is rejected");
        check(!emu::frontend::decodeGameGenie("").has_value(),
              "an empty code is rejected");

        // Decoding is case insensitive, since codes are printed upper case but
        // typed either way.
        const auto upper = emu::frontend::decodeGameGenie("ZEXPYG");
        const auto lower = emu::frontend::decodeGameGenie("zexpyg");
        check(upper.has_value() && lower.has_value() &&
                  upper->address == lower->address &&
                  upper->value == lower->value,
              "decoding is case insensitive");
    }

    group("applying patches");
    {
        std::vector<Cheat> cheats;

        Cheat plain;
        plain.address = 0x0100;
        plain.value = 0x63;
        cheats.push_back(plain);

        Cheat compared;
        compared.address = 0x0200;
        compared.value = 0xEA;
        compared.compare = 0xA9;
        cheats.push_back(compared);

        check(emu::frontend::applyCheats(cheats, 0x0100, 0x01) == 0x63,
              "a plain patch replaces the byte");
        check(emu::frontend::applyCheats(cheats, 0x0101, 0x01) == 0x01,
              "an unrelated address is untouched");
        check(emu::frontend::applyCheats(cheats, 0x0200, 0xA9) == 0xEA,
              "a compare patch fires on a match");
        check(emu::frontend::applyCheats(cheats, 0x0200, 0x4C) == 0x4C,
              "a compare patch is skipped when the byte differs");

        cheats[0].enabled = false;
        check(emu::frontend::applyCheats(cheats, 0x0100, 0x01) == 0x01,
              "a disabled cheat does nothing");
    }

    group("cheat files");
    {
        const std::string game = "cheat_test_game.nes";
        const std::string file = "cheat_test_game.cht";

        {
            std::ofstream out(file, std::ios::trunc);
            out << "# a comment line\n"
                << "\n"
                << "0100:63 Infinite lives\n"
                << "0200?A9:EA   Skip the check\n"
                << "-0300:01 Disabled for now\n"
                << "AAAAAA A game genie code\n"
                << "NOTACODE\n";
        }

        const auto cheats = emu::frontend::loadCheatFile(game);
        check(cheats.size() == 4,
              "comments, blanks and unreadable lines are skipped");

        if (cheats.size() == 4) {
            check(cheats[0].address == 0x0100 && cheats[0].value == 0x63,
                  "a raw patch parses");
            check(cheats[0].description == "Infinite lives",
                  "a trailing description is kept");
            check(cheats[1].compare.has_value() &&
                      *cheats[1].compare == 0xA9 && cheats[1].value == 0xEA,
                  "a compare patch parses");
            check(!cheats[2].enabled,
                  "a leading dash disables a line");
            check(cheats[3].address == 0x8000,
                  "a Game Genie line decodes");
        }

        check(emu::frontend::saveCheatFile(game, cheats),
              "a cheat list writes back");

        const auto reloaded = emu::frontend::loadCheatFile(game);
        bool same = reloaded.size() == cheats.size();
        for (size_t i = 0; same && i < reloaded.size(); ++i) {
            same = reloaded[i].address == cheats[i].address &&
                   reloaded[i].value == cheats[i].value &&
                   reloaded[i].enabled == cheats[i].enabled &&
                   reloaded[i].compare.has_value() ==
                       cheats[i].compare.has_value();
        }
        check(same, "a written cheat list reads back the same");

        std::error_code ignored;
        std::filesystem::remove(file, ignored);

        check(emu::frontend::loadCheatFile(game).empty(),
              "a missing cheat file yields no cheats");
    }

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
