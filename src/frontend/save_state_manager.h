#pragma once

#include <filesystem>
#include <string>

namespace emu::frontend {

class SaveStateManager {
public:
    explicit SaveStateManager(std::filesystem::path root);

    bool save(int slot,
              const std::string& game_path,
              const std::string& state_data,
              std::string& error);

    bool load(int slot,
              const std::string& game_path,
              std::string& state_data,
              std::string& error) const;

    bool exists(int slot, const std::string& game_path) const;

private:
    std::filesystem::path statePath(
        int slot,
        const std::string& game_path) const;

    std::filesystem::path root_;
};

}