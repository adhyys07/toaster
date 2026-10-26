#pragma once
 
#include "system.h"
 
#include <string>
#include <vector>

namespace emu {
    struct CoreInfo {
        const char* name;
    std::vector<std::string> extensions;  // lowercase, no dot
    SystemPtr (*create)();
};


const std::vector<CoreInfo>& registeredCores();

SystemPtr createForFile(const std::string& path, std::string& error);

}