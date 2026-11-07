#include "screenshot.h"

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend {
namespace {

namespace fs = std::filesystem;

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) return "unknown";

    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", local);
    return text;
}

std::vector<fs::path> collect(const std::string& prefix) {
    std::vector<fs::path> found;
    std::error_code ec;
    const fs::path directory = screenshotsDirectory();
    if (!fs::is_directory(directory, ec)) return found;

    for (const fs::directory_entry& entry :
         fs::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".png") continue;
        if (!prefix.empty()) {
            const std::string name = entry.path().filename().string();
            if (name.rfind(prefix + "-", 0) != 0) continue;
        }
        found.push_back(entry.path());
    }

    // Newest first. The filename carries a sortable timestamp, but mtime is
    // authoritative and costs nothing here, and it keeps working for files
    // that were renamed by hand.
    std::sort(found.begin(), found.end(),
              [](const fs::path& a, const fs::path& b) {
                  std::error_code left;
                  std::error_code right;
                  const auto at = fs::last_write_time(a, left);
                  const auto bt = fs::last_write_time(b, right);
                  if (left || right) return a.filename() > b.filename();
                  return at > bt;
              });
    return found;
}

} // namespace

// Screenshots go beside the executable so a portable install keeps them with
// the binary, matching where save states are written.
fs::path screenshotsDirectory() {
    fs::path root;
    if (char* base = SDL_GetBasePath()) {
        root = base;
        SDL_free(base);
    } else {
        root = fs::current_path();
    }
    return root / "screenshots";
}

bool savePngTo(const VideoFrame& frame, const fs::path& path,
               std::string& error) {
    if (!frame.pixels || frame.width <= 0 || frame.height <= 0) {
        error = "The core has not produced a frame yet.";
        return false;
    }

    std::error_code filesystem_error;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error) {
            error = "Could not create " + path.parent_path().string() + ": " +
                    filesystem_error.message();
            return false;
        }
    }

    // Cores hand out a const framebuffer. SDL needs a mutable pointer for the
    // surface wrapper even though saving only reads it, hence the cast.
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(frame.pixels), frame.width, frame.height, 32,
        frame.width * static_cast<int>(sizeof(uint32_t)),
        SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        error = std::string("Could not wrap the frame: ") + SDL_GetError();
        return false;
    }

    const bool saved = IMG_SavePNG(surface, path.string().c_str()) == 0;
    SDL_FreeSurface(surface);

    if (!saved) {
        error = std::string("Could not write the image: ") + IMG_GetError();
        return false;
    }
    return true;
}

std::string saveScreenshot(const VideoFrame& frame,
                           const std::string& game_path,
                           std::string& error) {
    const fs::path directory = screenshotsDirectory();
    const std::string stem = fs::path(game_path).stem().string();
    const std::string stamp = timestamp();

    // A second-resolution timestamp collides when the key is pressed twice in
    // the same second, so a counter disambiguates rather than overwriting.
    std::error_code exists_error;
    fs::path path;
    for (int sequence = 0; sequence < 1000; ++sequence) {
        path = directory /
               (stem + "-" + stamp +
                (sequence == 0 ? "" : "-" + std::to_string(sequence)) + ".png");
        if (!fs::exists(path, exists_error)) break;
    }

    if (!savePngTo(frame, path, error)) return {};
    return path.string();
}

std::vector<fs::path> listScreenshots(const std::string& game_path) {
    return collect(fs::path(game_path).stem().string());
}

std::vector<fs::path> listAllScreenshots() { return collect({}); }

} // namespace emu::frontend
