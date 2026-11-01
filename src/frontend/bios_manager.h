#pragma once

#include <filesystem>
#include <string>


namespace emu::frontend {

enum class BiosSystem {
    Ps1,
    Ps2
};

struct BiosStatus {
    bool installed = false;
    std::filesystem::path path;
    std::string message;
};

class BiosManager {
public:
    explicit BiosManager(std::filesystem::path system_root);

    BiosStatus check(BiosSystem system) const;
    bool import(BiosSystem system,
                const std::filesystem::path& source,
                std::string& error);

private:
    std::filesystem::path directory(BiosSystem system) const;
    std::filesystem::path root_;
};

}