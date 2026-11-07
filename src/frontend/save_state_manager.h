#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>

namespace emu::frontend {

struct SlotInfo {
    bool exists = false;
    std::time_t saved_at = 0;
    std::uintmax_t bytes = 0;
    std::filesystem::path thumbnail;
};

class SaveStateManager {
public:
    explicit SaveStateManager(std::filesystem::path root);

    static constexpr int kAutoSlot = 0;
    static constexpr int kSlotCount = 10;

    bool save(int slot,
              const std::string& game_path,
              const std::string& state_data,
              std::string& error);

    bool load(int slot,
              const std::string& game_path,
              std::string& state_data,
              std::string& error) const;

    bool exists(int slot, const std::string& game_path) const;

    SlotInfo info(int slot, const std::string& game_path) const;

    std::filesystem::path thumbnailPath(
        int slot, const std::string& game_path) const;

    // Deletes the state and its thumbnail. True when the slot is gone
    // afterwards, including when it was already empty.
    bool remove(int slot, const std::string& game_path, std::string& error);

private:
    std::filesystem::path statePath(
        int slot,
        const std::string& game_path) const;

    std::filesystem::path root_;
};

} // namespace emu::frontend
