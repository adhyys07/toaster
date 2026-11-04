#include "game_library.h"

#include <system_error>
#include <utility>

namespace emu::frontend {

std::string libraryPathKey(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal().string();
}

GameLibrary::GameLibrary(std::string server_url)
    : client_(std::move(server_url)) {}

bool GameLibrary::scanFolder(
    const std::string& folder,
    std::string& error) {
    if (!client_.scan(folder, error))
        return false;

    return refresh(error);
}

bool GameLibrary::refresh(std::string& error) {
    if (!client_.listGames(games_, error))
        return false;

    // Stale totals next to a fresh list would misreport the library, so a
    // failure here clears them rather than leaving the previous numbers.
    std::string statistics_error;
    if (!client_.getStatistics(statistics_, statistics_error))
        statistics_ = LibraryStatistics{};

    return true;
}

bool GameLibrary::toggleFavorite(
    const RemoteGame& game,
    std::string& error) {
    return setFavorite(game, !game.favorite, error);
}

bool GameLibrary::setFavorite(
    const RemoteGame& game,
    bool favorite,
    std::string& error) {
    if (!client_.setFavorite(game.id, favorite, error))
        return false;

    return refresh(error);
}

bool GameLibrary::markPlayed(
    const RemoteGame& game,
    std::string& error) {
    return client_.markPlayed(game.id, error);
}

const std::vector<RemoteGame>& GameLibrary::games() const {
    return games_;
}

const LibraryStatistics& GameLibrary::statistics() const {
    return statistics_;
}

const RemoteGame* GameLibrary::find(
    const std::filesystem::path& path) const {
    const std::string wanted = libraryPathKey(path);
    for (const RemoteGame& game : games_) {
        if (libraryPathKey(game.path) == wanted) return &game;
    }
    return nullptr;
}

}
