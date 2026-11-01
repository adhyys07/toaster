#include "bios_manager.h"

#include <fstream>
#include <utility>

namespace emu::frontend {

BiosManager::BiosManager(std::filesystem::path system_root)
    : root_(std::move(system_root)) {}

std::filesystem::path BiosManager::directory(BiosSystem system) const {
    switch (system) {
    case BiosSystem::Ps1:
        return root_ / "swanstation" / "bios";
    case BiosSystem::Ps2:
        return root_ / "pcsx2" / "bios";
    }
    return {};
}

BiosStatus BiosManager::check(BiosSystem system) const {
    const auto path = directory(system);
    std::error_code error;

    if (!std::filesystem::is_directory(path, error)) {
        if (error) {
            return {false, path,
                    "Could not inspect BIOS directory: " + error.message()};
        }
        return {false, path, "BIOS directory is missing"};
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(path, error)) {
        if (error) {
            return {false, path,
                    "Could not inspect BIOS directory: " + error.message()};
        }
        if (entry.is_regular_file(error) && !error) {
            return {true, entry.path(), "BIOS file found"};
        }
        error.clear();
    }

    if (error) {
        return {false, path,
                "Could not inspect BIOS directory: " + error.message()};
    }
    return {false, path, "No BIOS file found"};
}

bool BiosManager::import(BiosSystem system,
                         const std::filesystem::path& source,
                         std::string& error) {
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(source, filesystem_error) ||
        filesystem_error) {
        error = filesystem_error
            ? "Could not read BIOS source: " + filesystem_error.message()
            : "BIOS source is not a regular file.";
        return false;
    }

    const auto destination_directory = directory(system);
    std::filesystem::create_directories(
        destination_directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create BIOS directory: " +
                filesystem_error.message();
        return false;
    }

    const auto destination =
        destination_directory / source.filename();
    if (std::filesystem::equivalent(source, destination, filesystem_error) &&
        !filesystem_error) {
        return true;
    }
    filesystem_error.clear();

    const auto temporary = destination.string() + ".import";
    std::filesystem::copy_file(
        source, temporary,
        std::filesystem::copy_options::overwrite_existing,
        filesystem_error);
    if (filesystem_error) {
        error = "Could not copy BIOS file: " + filesystem_error.message();
        return false;
    }

    std::filesystem::remove(destination, filesystem_error);
    filesystem_error.clear();
    std::filesystem::rename(temporary, destination, filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(temporary);
        error = "Could not install BIOS file: " + filesystem_error.message();
        return false;
    }
    return true;
}

} // namespace emu::frontend