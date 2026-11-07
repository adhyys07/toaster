#pragma once

#include "theme.h"
#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend::ui {

bool parseColor(const std::string& text, SDL_Color& out);
std::string formatColor(SDL_Color color);

bool loadTheme(const std::filesystem::path& file, Theme& theme,
               std::vector<std::string>& warnings, std::string& error);

bool saveTheme(const std::filesystem::path& file, const Theme& theme,
               std::string& error);

float relativeLuminance(SDL_Color color);
float contrastRatio(SDL_Color a, SDL_Color b);

struct ThemeProblem
{
    std::string field;
    std::string message;
    float ratio = 0.0f;
    bool fatal = false;
};

std::vector<ThemeProblem> validateTheme(const Theme& theme);


Theme repairTheme(Theme theme);

}