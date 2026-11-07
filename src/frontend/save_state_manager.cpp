#include "save_state_manager.h"

#include <fstream>
#include <functional>
#include <utility>
#include <chrono>
#include <system_error>

namespace emu::frontend {

SaveStateManager::SaveStateManager(std::filesystem::path root)
    : root_(std::move(root)) {}

std::filesystem::path SaveStateManager::statePath(
    int slot,
    const std::string& game_path) const {
    const auto id = std::to_string(
        std::hash<std::string>{}(game_path));

    return root_ / (id + "_slot" + std::to_string(slot) + ".state");
}

bool SaveStateManager::save(
    int slot,
    const std::string& game_path,
    const std::string& state_data,
    std::string& error) {
    if (slot < 0 || slot > 9) {
        error = "Save-state slot must be between 0 and 9.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(root_, ec);

    if (ec) {
        error = "Could not create save-state directory: " + ec.message();
        return false;
    }

    const auto path = statePath(slot, game_path);
    const auto temporary = path.string() + ".tmp";

    {
        std::ofstream output(temporary, std::ios::binary);
        if (!output) {
            error = "Could not create save-state file.";
            return false;
        }

        output.write(state_data.data(),
                     static_cast<std::streamsize>(state_data.size()));

        if (!output) {
            error = "Could not write save-state file.";
            return false;
        }
    }

    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "Could not finalize save-state file: " + ec.message();
        return false;
    }

    return true;
}

bool SaveStateManager::load(
    int slot,
    const std::string& game_path,
    std::string& state_data,
    std::string& error) const {
    const auto path = statePath(slot, game_path);

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Save-state does not exist.";
        return false;
    }

    state_data.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());

    if (state_data.empty()) {
        error = "Save-state file is empty.";
        return false;
    }

    return true;
}

bool SaveStateManager::exists(
    int slot,
    const std::string& game_path) const {
    return std::filesystem::is_regular_file(
        statePath(slot, game_path));
}

std::filesystem::path SaveStateManager::thumbnailPath(
    int slot,
    const std::string& game_path) const {
    std::filesystem::path path = statePath(slot, game_path);
    path.replace_extension(".png");
    return path;
}

SlotInfo SaveStateManager::info(int slot,
                                const std::string& game_path) const {
    SlotInfo out;
    const auto path = statePath(slot, game_path);

    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return out;

    out.exists = true;
    out.bytes = std::filesystem::file_size(path, ec);
    if (ec) out.bytes = 0;

    ec.clear();
    const auto written = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        const auto file_now = std::filesystem::file_time_type::clock::now();
        const auto age = file_now - written;
        out.saved_at = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now() -
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                age));
    }

    const auto thumbnail = thumbnailPath(slot, game_path);
    ec.clear();
    if (std::filesystem::is_regular_file(thumbnail, ec)) {
        out.thumbnail = thumbnail;
    }
    return out;
}

bool SaveStateManager::remove(int slot,
                              const std::string& game_path,
                              std::string& error) {
    std::error_code ec;
    std::filesystem::remove(thumbnailPath(slot, game_path), ec);

    ec.clear();
    std::filesystem::remove(statePath(slot, game_path), ec);
    if (ec) {
        error = "Could not delete the save state: " + ec.message();
        return false;
    }
    error.clear();
    return true;
}


}