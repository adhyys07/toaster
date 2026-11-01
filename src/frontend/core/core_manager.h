#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace emu::frontend {

enum class CoreSystem {
    Ps1,
    Psp,
    Ps2
};

struct CoreInfo {
    CoreSystem system;
    std::filesystem::path path;
    std::string filename;
};

class CoreManager {
public:
    explicit CoreManager(std::filesystem::path root);

    std::vector<CoreInfo> discover() const;
    std::optional<CoreInfo> find(CoreSystem system) const;

private:
    std::filesystem::path root_;
};

}