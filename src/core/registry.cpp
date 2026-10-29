#include "registry.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace emu {
 
SystemPtr createNesSystem();
SystemPtr createChip8System();
 
const std::vector<CoreInfo>& registeredCores() {
    static const std::vector<CoreInfo> cores = {
        { "NES",     { "nes" },         createNesSystem   },
        { "CHIP-8",  { "ch8", "c8" },   createChip8System },
    };
    return cores;
}

namespace {
 
std::string extensionOf(const std::string& path) {
    const size_t dot   = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos) return {};
    if (slash != std::string::npos && dot < slash) return {};
 
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

const char* sniff(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;
 
    char magic[4] = {};
    file.read(magic, 4);
    if (file.gcount() == 4 && std::memcmp(magic, "NES\x1A", 4) == 0) return "NES";
    return nullptr;
}

}
 
SystemPtr createForFile(const std::string& path, std::string& error) {
    error.clear();
    const std::string ext = extensionOf(path);
 
    for (const CoreInfo& core : registeredCores()) {
        if (std::find(core.extensions.begin(), core.extensions.end(), ext) !=
            core.extensions.end()) {
            return core.create();
        }
    }
 
    if (const char* sniffed = sniff(path)) {
        for (const CoreInfo& core : registeredCores()) {
            if (std::strcmp(core.name, sniffed) == 0) return core.create();
        }
    }
 
    error = ext.empty()
        ? "cannot tell what system '" + path + "' is for (no file extension)"
        : "no core handles '." + ext + "' files";
    error += "\nknown extensions:";
    for (const CoreInfo& core : registeredCores()) {
        for (const std::string& e : core.extensions) error += " ." + e;
    }
    return nullptr;
}
 
}
