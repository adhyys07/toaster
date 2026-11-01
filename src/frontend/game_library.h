#pragma once

#include "library_client.h"

#include <string>
#include <vector>

namespace emu::frontend {

class GameLibrary {
public:
    explicit GameLibrary(std::string server_url);

    bool scanFolder(
        const std::string& folder,
        std::string& error);

    bool refresh(std::string& error);

    bool toggleFavorite(
        const RemoteGame& game,
        std::string& error);

    bool markPlayed(
        const RemoteGame& game,
        std::string& error);

    const std::vector<RemoteGame>& games() const;

private:
    LibraryClient client_;
    std::vector<RemoteGame> games_;
};

}