#pragma once
#include <SDL_ttf.h>
#include <string>

namespace emu::frontend {
TTF_Font* openUiFont(int point_size, std::string& error);
}