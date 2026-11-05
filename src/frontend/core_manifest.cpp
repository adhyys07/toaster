#include "core_manifest.h"

#include <cctype>

namespace emu::frontend {

CoreManifest parseCoreManifest(std::istream& input) {
    CoreManifest manifest;

    std::string digest;
    std::string filename;
    while (input >> digest >> filename) {
        // sha256sum in binary mode, which is the default on Windows, writes
        // "<hash> *name"; the asterisk is a mode marker, not part of the name.
        if (!filename.empty() && filename.front() == '*') {
            filename.erase(0, 1);
        }
        // Tolerate a manifest saved with CRLF line endings.
        while (!filename.empty() &&
               (filename.back() == '\r' || filename.back() == '\n')) {
            filename.pop_back();
        }

        for (char& c : digest) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        // A digest that is not 64 hex characters cannot verify anything, so
        // dropping it fails closed instead of matching something by accident.
        const bool valid_digest =
            digest.size() == 64 &&
            digest.find_first_not_of("0123456789abcdef") == std::string::npos;
        if (!valid_digest) continue;

        // Manifests name bare files. Anything with a path in it could place a
        // download outside the cores directory.
        if (filename.empty() ||
            filename.find('/') != std::string::npos ||
            filename.find('\\') != std::string::npos ||
            filename.find("..") != std::string::npos) {
            continue;
        }

        manifest[filename] = digest;
    }

    return manifest;
}

} // namespace emu::frontend
