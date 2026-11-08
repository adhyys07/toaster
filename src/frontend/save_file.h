#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend {

struct SaveFile
{
    std::filesystem::path path;
    std::string label;
    std::uintmax_t bytes = 0;
    long long modified = 0;
    bool is_backup = false;
};

std::vector<SaveFile> listSaveFiles(const std::string& rom_path);

bool restoreBackup(const SaveFile& backup, const std::string& rom_path, std::string& error);

bool deleteSaveFile(const SaveFile& file, std::string& error);

std::string formatBytes(std::uintmax_t bytes);

}