#include "save_file.h"

#include "save_backup.h"

#include <chrono>
#include <cstdio>
#include <system_error>

namespace emu::frontend {
namespace {

// file_clock has no direct conversion to system_clock before C++20's
// clock_cast, so this is the usual workaround: the two clocks' "now" are
// assumed to coincide, which is accurate enough for a display timestamp and
// never needs to be exact down to the second.
long long toUnixSeconds(std::filesystem::file_time_type time) {
    using namespace std::chrono;
    const auto system_time =
        time_point_cast<system_clock::duration>(
            time - std::filesystem::file_time_type::clock::now() +
            system_clock::now());
    return static_cast<long long>(system_clock::to_time_t(system_time));
}

} // namespace

std::vector<SaveFile> listSaveFiles(const std::string& rom_path) {
    std::vector<SaveFile> found;
    std::error_code ec;

    // Derived the same way Cartridge::saveBattery derives it, so the two can
    // never disagree about which file is this game's save.
    const std::filesystem::path live =
        std::filesystem::path(rom_path).replace_extension(".srm");

    const auto describe = [&](const std::filesystem::path& path,
                              std::string label, bool is_backup) {
        if (!std::filesystem::is_regular_file(path, ec)) return;
        SaveFile file;
        file.path = path;
        file.label = std::move(label);
        file.bytes = std::filesystem::file_size(path, ec);
        file.is_backup = is_backup;
        file.modified = toUnixSeconds(
            std::filesystem::last_write_time(path, ec));
        found.push_back(std::move(file));
    };

    describe(live, "Battery save", false);

    // rotateBackups writes at most nine (.bak0 through .bak8); probing a
    // couple past that costs nothing and survives the cap changing later.
    for (int i = 0; i < 12; ++i) {
        std::filesystem::path backup = live;
        backup += ".bak" + std::to_string(i);
        describe(backup, "Backup " + std::to_string(i + 1), true);
    }

    return found;
}

bool restoreBackup(const SaveFile& backup, const std::string& rom_path,
                   std::string& error) {
    error.clear();
    if (!backup.is_backup) {
        error = "That is the live save, not a backup.";
        return false;
    }

    const std::filesystem::path live =
        std::filesystem::path(rom_path).replace_extension(".srm");

    // The live file is backed up before being overwritten, so restoring the
    // wrong backup is itself undoable: Restore is always safe to try.
    std::string rotate_error;
    rotateBackups(live, 9, rotate_error);

    std::error_code ec;
    std::filesystem::copy_file(
        backup.path, live, std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
        error = "Could not restore " + backup.label + ": " + ec.message();
        return false;
    }
    return true;
}

bool deleteSaveFile(const SaveFile& file, std::string& error) {
    error.clear();
    std::error_code ec;
    if (!std::filesystem::remove(file.path, ec)) {
        error = "Could not delete " + file.label + ": " +
                (ec ? ec.message() : std::string("file not found"));
        return false;
    }
    return true;
}

std::string formatBytes(std::uintmax_t bytes) {
    constexpr std::uintmax_t kKiB = 1024;
    constexpr std::uintmax_t kMiB = kKiB * 1024;

    char buffer[32] = {};
    if (bytes >= kMiB) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                      static_cast<double>(bytes) / static_cast<double>(kMiB));
    } else if (bytes >= kKiB) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB",
                      static_cast<double>(bytes) / static_cast<double>(kKiB));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B",
                      static_cast<unsigned long long>(bytes));
    }
    return buffer;
}

} // namespace emu::frontend
