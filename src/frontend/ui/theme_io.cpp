#include "theme_io.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace emu::frontend::ui {
namespace {

bool hexValue(char ch, int& out) {
    if (ch >= '0' && ch <= '9') { out = ch - '0'; return true; }
    if (ch >= 'a' && ch <= 'f') { out = ch - 'a' + 10; return true; }
    if (ch >= 'A' && ch <= 'F') { out = ch - 'A' + 10; return true; }
    return false;
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

float channelLuminance(Uint8 value) {
    const float c = value / 255.0f;
    return c <= 0.03928f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

}

bool parseColor(const std::string& text, SDL_Color& out) {
    std::string body = trim(text);
    if (!body.empty() && body.front() == '#') body.erase(body.begin());

    if (body.size() != 3 && body.size() != 6 && body.size() != 8) return false;

    int digits[8]{};
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (!hexValue(body[i], digits[i])) return false;
    }

    if (body.size() == 3) {
        out.r = static_cast<Uint8>(digits[0] * 17);
        out.g = static_cast<Uint8>(digits[1] * 17);
        out.b = static_cast<Uint8>(digits[2] * 17);
        out.a = 255;
        return true;
    }

    out.r = static_cast<Uint8>(digits[0] * 16 + digits[1]);
    out.g = static_cast<Uint8>(digits[2] * 16 + digits[3]);
    out.b = static_cast<Uint8>(digits[4] * 16 + digits[5]);
    out.a = body.size() == 8
                ? static_cast<Uint8>(digits[6] * 16 + digits[7])
                : 255;
    return true;
}

std::string formatColor(SDL_Color color) {
    static const char* kDigits = "0123456789abcdef";
    std::string text = "#";

    const Uint8 channels[4] = {color.r, color.g, color.b, color.a};
    const int count = color.a == 255 ? 3 : 4;

    for (int i = 0; i < count; ++i) {
        text.push_back(kDigits[channels[i] >> 4]);
        text.push_back(kDigits[channels[i] & 0x0F]);
    }
    return text;
}

bool loadTheme(const std::filesystem::path& file, Theme& theme,
               std::vector<std::string>& warnings, std::string& error) {
    std::ifstream input(file);
    if (!input) {
        error = "could not open " + file.string();
        return false;
    }
    Theme parsed = theme;
    std::string line;
    int line_number = 0;
    int applied = 0;

    while (std::getline(input, line)) {
        ++line_number;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        const auto equals = trimmed.find('=');
        if (equals == std::string::npos) {
            warnings.push_back("line " + std::to_string(line_number) +
                               ": no '=', ignored");
            continue;
        }

        const std::string key = trim(trimmed.substr(0, equals));
        const std::string value = trim(trimmed.substr(equals + 1));

        const ThemeField* field = nullptr;
        for (int i = 0; i < kThemeFieldCount; ++i) {
            if (key == kThemeFields[i].key) {
                field = &kThemeFields[i];
                break;
            }
        }
        if (!field) {
            warnings.push_back("line " + std::to_string(line_number) +
                               ": unknown colour '" + key + "', ignored");
            continue;
        }

        SDL_Color color{};
        if (!parseColor(value, color)) {
            warnings.push_back("line " + std::to_string(line_number) + ": '" +
                               value + "' is not a colour, ignored");
            continue;
        }

        parsed.*(field->member) = color;
        ++applied;
    }

    if (applied == 0) {
        error = file.string() + " set no colours";
        return false;
    }

    theme = parsed;
    return true;
}

bool saveTheme(const std::filesystem::path& file, const Theme& theme,
               std::string& error) {
    std::error_code ignored;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ignored);
    }

    std::ofstream output(file, std::ios::trunc);
    if (!output) {
        error = "could not write " + file.string();
        return false;
    }

    output << "# Toaster theme\n"
              "#\n"
              "# Colours are #rgb, #rrggbb or #rrggbbaa. Set theme=custom in\n"
              "# settings.ini to use this file. Any colour left out keeps its\n"
              "# built-in value, so a file may hold only the ones you change.\n"
              "\n";
    
    for (int i = 0; i < kThemeFieldCount; ++i) {
        const ThemeField& field = kThemeFields[i];
        output << "# " << field.label << '\n'
               << field.key << '=' << formatColor(theme.*(field.member))
               << '\n';
    }

    if (!output.good()) {
        error = "could not write " + file.string();
        return false;
    }
    return true;
}

float relativeLuminance(SDL_Color color) {
    return 0.2126f * channelLuminance(color.r) +
           0.7152f * channelLuminance(color.g) +
           0.0722f * channelLuminance(color.b);
}

float contrastRatio(SDL_Color a, SDL_Color b) {
    const float la = relativeLuminance(a);
    const float lb = relativeLuminance(b);
    const float lighter = std::max(la, lb);
    const float darker = std::min(la, lb);
    return (lighter + 0.05f) / (darker + 0.05f);
}

std::vector<ThemeProblem> validateTheme(const Theme& theme) {
    struct Pair {
        const char* field;
        SDL_Color foreground;
        SDL_Color background;
        const char* where;
    };

    const Pair pairs[] = {
        {"text", theme.text, theme.surface, "text on a panel"},
        {"text", theme.text, theme.bg, "text on the window"},
        {"text", theme.text, theme.accent_soft, "text on a selected row"},
        {"text_muted", theme.text_muted, theme.surface,
         "secondary text on a panel"},
        {"text_on_accent", theme.text_on_accent, theme.accent,
         "text on the accent"},
        {"gold", theme.gold, theme.surface, "the title and favorites"},
        // danger and success are the toast's border and dot, not its text,
        // so they are held to WCAG's 3:1 non-text minimum below rather than
        // the 4.5:1 body-text bar.
        {"danger", theme.danger, theme.surface_hover, "an error marker"},
        {"success", theme.success, theme.surface_hover, "a success marker"},
        {"border", theme.border, theme.surface, "a panel border"},
    };

    std::vector<ThemeProblem> problems;

    for (const Pair& pair : pairs) {
        const float ratio = contrastRatio(pair.foreground, pair.background);

        // Only text is held to the 4.5:1 body-text bar. A border is pure
        // decoration and needs to be merely visible; the toast markers are UI
        // components, which WCAG puts at 3:1. Holding an indicator to the
        // text bar rejects palettes that read perfectly well.
        const std::string field(pair.field);
        float threshold = 4.5f;
        if (field == "border") {
            threshold = 1.3f;
        } else if (field == "danger" || field == "success") {
            threshold = 3.0f;
        }

        if (ratio >= threshold) continue;

        ThemeProblem problem;
        problem.field = pair.field;
        problem.ratio = ratio;
        problem.fatal = ratio < 1.6f;
        problem.message =
            std::string(problem.fatal ? "unreadable: " : "low contrast: ") +
            pair.where + " is " +
            std::to_string(static_cast<int>(ratio * 10) / 10.0f).substr(0, 3) +
            ":1";
        problems.push_back(std::move(problem));
    }

    return problems;

}

Theme repairTheme(Theme theme) {
    const auto legible = [](SDL_Color& foreground, SDL_Color background) {
        if (contrastRatio(foreground, background) >= 1.6f) return;
        const SDL_Color white{255, 255, 255, foreground.a};
        const SDL_Color black{0, 0, 0, foreground.a};
        foreground = contrastRatio(white, background) >=
                             contrastRatio(black, background)
                         ? white
                         : black;
    };

    legible(theme.text, theme.surface);
    legible(theme.text, theme.bg);
    legible(theme.text_muted, theme.surface);
    legible(theme.text_on_accent, theme.accent);
    legible(theme.gold, theme.surface);

    if (contrastRatio(theme.border, theme.surface) < 1.05f) {
        theme.border = relativeLuminance(theme.surface) > 0.5f
                           ? SDL_Color{0, 0, 0, 60}
                           : SDL_Color{255, 255, 255, 60};
    }

    return theme;
}

}