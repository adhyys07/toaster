#include "screenshot.h"

#include <SDL.h>
#include <SDL_image.h>

#include <cstdint>
#include <ctime>
#include <filesystem>

namespace emu::frontend {
namespace {

namespace fs = std::filesystem;

// Screenshots go beside the executable so a portable install keeps them with
// the binary, matching where save states are written.
fs::path screenshotDirectory() {
    fs::path root;
    if (char* base = SDL_GetBasePath()) {
        root = base;
        SDL_free(base);
    } else {
        root = fs::current_path();
    }
    return root / "screenshots";
}

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) return "unknown";

    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", local);
    return text;
}

} // namespace

std::string saveScreenshot(const VideoFrame& frame,
                           const std::string& game_path,
                           std::string& error) {
    if (!frame.pixels || frame.width <= 0 || frame.height <= 0) {
        error = "The core has not produced a frame yet.";
        return {};
    }

    std::error_code filesystem_error;
    const fs::path directory = screenshotDirectory();
    fs::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the screenshots folder: " +
                filesystem_error.message();
        return {};
    }

    // Cores hand out a const framebuffer. SDL needs a mutable pointer for the
    // surface wrapper even though saving only reads it, hence the cast.
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(frame.pixels), frame.width, frame.height, 32,
        frame.width * static_cast<int>(sizeof(uint32_t)),
        SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        error = std::string("Could not wrap the frame: ") + SDL_GetError();
        return {};
    }

    const std::string stem = fs::path(game_path).stem().string();
    const std::string stamp = timestamp();

    // A second-resolution timestamp collides when the key is pressed twice in
    // the same second, so a counter disambiguates rather than overwriting.
    fs::path path;
    for (int sequence = 0; sequence < 1000; ++sequence) {
        path = directory /
               (stem + "-" + stamp +
                (sequence == 0 ? "" : "-" + std::to_string(sequence)) + ".png");
        if (!fs::exists(path, filesystem_error)) break;
    }

    const bool saved = IMG_SavePNG(surface, path.string().c_str()) == 0;
    SDL_FreeSurface(surface);

    if (!saved) {
        error = std::string("Could not write the screenshot: ") + IMG_GetError();
        return {};
    }
    return path.string();
}

} // namespace emu::frontend
