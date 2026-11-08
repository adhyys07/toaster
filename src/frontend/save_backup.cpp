#include "save_backup.h"

#include <system_error>

namespace emu::frontend
{

bool rotateBackups(const std::filesystem::path& file, int keep,
                   std::string& error) {
    error.clear();
    if (keep <= 0) return true;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        // Nothing to protect yet. The first save of a new game lands here.
        return true;
    }

    const auto slot = [&](int index) {
        std::filesystem::path path = file;
        path += ".bak" + std::to_string(index);
        return path;
    };

    std::filesystem::remove(slot(keep - 1), ec);
    for (int i = keep - 1; i > 0; --i) {
        if (!std::filesystem::is_regular_file(slot(i - 1), ec)) continue;
        std::filesystem::rename(slot(i - 1), slot(i), ec);
        if (ec) {
            error = "Could not rotate " + slot(i - 1).string() + ": " +
                    ec.message();
            return false;
        }
    }

    std::filesystem::copy_file(
        file, slot(0), std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "Could not back up " + file.string() + ": " + ec.message();
        return false;
    }
    return true;
}


} // namespace emu::frontend
