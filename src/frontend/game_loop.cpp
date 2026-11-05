// in game_loop.cpp, called on F10
bool saveScreenshot(const emu::VideoFrame& frame, const std::string& rom_path) {
    if (!frame.pixels || frame.width <= 0 || frame.height <= 0) return false;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(frame.pixels), frame.width, frame.height,
        32, frame.width * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return false;

    std::error_code ignored;
    std::filesystem::create_directories("screenshots", ignored);

    // Second-resolution timestamps collide on rapid presses, so add a counter.
    static int sequence = 0;
    const auto now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    const std::string path =
        "screenshots/" + std::filesystem::path(rom_path).stem().string() +
        "-" + stamp + "-" + std::to_string(sequence++) + ".png";

    const bool ok = IMG_SavePNG(surface, path.c_str()) == 0;
    SDL_FreeSurface(surface);
    if (ok) std::printf("Saved %s\n", path.c_str());
    return ok;
}
