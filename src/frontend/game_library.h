#pragma once

#include "library_client.h"

#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend {

// Canonical form of a game path, used to match a locally scanned file against
// the row the library service holds for it. Comparing raw strings would miss
// a match whenever one side is relative and the other absolute.
std::string libraryPathKey(const std::filesystem::path& path);

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

    bool setFavorite(
        const RemoteGame& game,
        bool favorite,
        std::string& error);
    
    bool setTags(const RemoteGame& game,
        const std::vector<std::string>& tags,
        std::string& error);

    bool markPlayed(
        const RemoteGame& game,
        std::string& error);

    bool addPlayTime(
        const RemoteGame& game,
        long long seconds,
        std::string& error);

    const std::vector<RemoteGame>& games() const;

    const LibraryStatistics& statistics() const;

    const RemoteGame* find(const std::filesystem::path& path) const;

private:
    LibraryClient client_;
    std::vector<RemoteGame> games_;
    LibraryStatistics statistics_;
};

}
