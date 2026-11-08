#include "palette_io.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace emu::frontend {
namespace {

Palette gb(const char* name, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return Palette{name, {a, b, c, d}};
}

} // namespace

std::vector<Palette> builtinPalettes(const char* system_name, int size) {
    const std::string system = system_name ? system_name : "";

    if (system == "GAME BOY" && size == 4) {
        return {
            gb("DMG green", 0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820),
            gb("Pocket grey", 0xFFE8E8E8, 0xFFA0A0A0, 0xFF585858, 0xFF181818),
            gb("Light amber", 0xFFFFF6D3, 0xFFF9A875, 0xFFEB6B6F, 0xFF7C3F58),
            gb("Blue tint", 0xFFE8F8F8, 0xFF88C0D0, 0xFF386888, 0xFF081828),
            gb("High contrast", 0xFFFFFFFF, 0xFFB0B0B0, 0xFF505050, 0xFF000000),
        };
    }

    (void)size;
    return {};
}

bool loadPalette(const std::filesystem::path& file, int size, Palette& out,
                 std::string& error) {
    std::ifstream input(file);
    if (!input) {
        error = "Could not open " + file.string();
        return false;
    }

    out.entries.clear();
    out.name = file.stem().string();

    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        while (!line.empty() && std::isspace(
                   static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }
        std::size_t begin = 0;
        while (begin < line.size() &&
               std::isspace(static_cast<unsigned char>(line[begin]))) {
            ++begin;
        }
        line = line.substr(begin);
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        unsigned value = 0;
        if (std::sscanf(line.c_str(), "%x", &value) != 1) {
            error = file.filename().string() + ": line " +
                    std::to_string(line_number) + " is not a hex colour.";
            return false;
        }
        out.entries.push_back(0xFF000000u | (value & 0x00FFFFFFu));
    }

    if (static_cast<int>(out.entries.size()) != size) {
        error = file.filename().string() + ": expected " +
                std::to_string(size) + " colours, found " +
                std::to_string(out.entries.size()) + ".";
        return false;
    }

    error.clear();
    return true;
}

std::vector<Palette> availablePalettes(const char* system_name, int size) {
    std::vector<Palette> out = builtinPalettes(system_name, size);
    if (size <= 0) return out;

    std::error_code ignored;
    const std::filesystem::path folder("palettes");
    if (!std::filesystem::is_directory(folder, ignored)) return out;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(folder, ignored)) {
        if (!entry.is_regular_file(ignored)) continue;

        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (extension != ".pal") continue;

        Palette loaded;
        std::string ignored_error;
        // A file sized for another core is simply not this core's palette, so
        // it is skipped silently rather than reported as broken.
        if (!loadPalette(entry.path(), size, loaded, ignored_error)) continue;

        const bool duplicate =
            std::any_of(out.begin(), out.end(), [&](const Palette& existing) {
                return existing.name == loaded.name;
            });
        if (!duplicate) out.push_back(std::move(loaded));
    }

    return out;
}

bool applyPalette(System& system, const std::string& name,
                  std::string& error) {
    error.clear();

    const int size = system.paletteSize();
    if (size <= 0) {
        // Not an error even when a name is set: most cores have no swappable
        // palette, and one shared setting should not nag on every launch of a
        // core that ignores it.
        return true;
    }
    if (name.empty()) return true;

    for (const Palette& palette : availablePalettes(system.name(), size)) {
        if (palette.name != name) continue;
        if (static_cast<int>(palette.entries.size()) != size) break;
        if (!system.setPalette(palette.entries.data(), size)) {
            error = "The core refused the palette \"" + name + "\".";
            return false;
        }
        return true;
    }

    error = "No palette named \"" + name + "\" for " +
            (system.name() ? system.name() : "this core") + ".";
    return false;
}

} // namespace emu::frontend
