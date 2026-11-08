#pragma once

// For Compat, which RemoteGame and GameEntry both carry.
#include "game_entry.h"

#include <string>
#include <vector>

namespace emu::frontend {

struct RemoteGame {
    int id = 0;
    std::string path;
    std::string title;
    std::string system;
    std::string extension;
    std::string artwork;
    bool favorite = false;
    long long last_played = 0;
    int play_count = 0;
    std::string tags;
    Compat compat = Compat::Unknown;
    long long seconds_played = 0;
};

struct LibraryStatistics {
    int total = 0;
    int favorites = 0;
    int recently_played = 0;
    int plays = 0;
};

struct CatalogGame {
    int id = 0;
    std::string title;
    std::string overview;
    std::string release_date;
    std::string platform;
    std::string artwork;
};

class LibraryClient {
public:
    explicit LibraryClient(std::string server_url);

    bool scan(const std::string& folder, std::string& error);

    bool listGames(
        std::vector<RemoteGame>& games,
        std::string& error);

    bool listFavorites(
        std::vector<RemoteGame>& games,
        std::string& error);

    bool listRecentlyPlayed(
        std::vector<RemoteGame>& games,
        std::string& error);

    bool setFavorite(
        int game_id,
        bool favorite,
        std::string& error);

    bool markPlayed(
        int game_id,
        std::string& error);

    bool setTags(
        int game_id,
        const std::vector<std::string>& tags,
        std::string& error);

    bool addPlayTime(
        int game_id,
        long long seconds,
        std::string& error);

    bool getStatistics(
        LibraryStatistics& statistics,
        std::string& error);

    bool searchCatalog(
        const std::string& query,
        std::vector<CatalogGame>& games,
        std::string& error);

private:
    std::string server_url_;
};

} // namespace emu::frontend