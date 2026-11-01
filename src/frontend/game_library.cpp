#include "game_library.h"

namespace emu::frontend {

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
    return client_.listGames(games_, error);
}

bool GameLibrary::toggleFavorite(
    const RemoteGame& game,
    std::string& error) {
    if (!client_.setFavorite(game.id, !game.favorite, error))
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

}