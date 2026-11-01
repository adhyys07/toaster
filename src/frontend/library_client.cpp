#include "library_client.h"

#include <cctype>
#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace emu::frontend {
namespace {

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void closeSocket(int socket_fd) {
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

bool request(const std::string& base_url, const std::string& method,
             const std::string& path, const std::string& body,
             std::string& response, std::string& error) {
    constexpr char prefix[] = "http://";
    if (base_url.compare(0, sizeof(prefix) - 1, prefix) != 0) {
        error = "Library server URL must start with http://";
        return false;
    }
    const std::string address = base_url.substr(sizeof(prefix) - 1);
    const auto slash = address.find('/');
    const std::string host_port = address.substr(0, slash);
    const auto colon = host_port.rfind(':');
    const std::string host = host_port.substr(0, colon);
    const std::string port = colon == std::string::npos
        ? "80" : host_port.substr(colon + 1);
    SocketRuntime runtime;
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses) != 0) {
        error = "Could not resolve library server";
        return false;
    }
    int socket_fd = -1;
    for (addrinfo* item = addresses; item; item = item->ai_next) {
        socket_fd = static_cast<int>(socket(item->ai_family, item->ai_socktype,
                                            item->ai_protocol));
        if (socket_fd >= 0 &&
            connect(socket_fd, item->ai_addr,
                    static_cast<int>(item->ai_addrlen)) == 0) break;
        if (socket_fd >= 0) closeSocket(socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo(addresses);
    if (socket_fd < 0) {
        error = "Could not connect to library server";
        return false;
    }
    const std::string target = slash == std::string::npos
        ? path : address.substr(slash) + path;
    const std::string message = method + " " + target + " HTTP/1.1\r\nHost: " +
        host + "\r\nConnection: close\r\nContent-Type: application/json\r\n" +
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < message.size()) {
        const int count = send(socket_fd, message.data() + sent,
                               static_cast<int>(message.size() - sent), 0);
        if (count <= 0) {
            closeSocket(socket_fd);
            error = "Could not send library request";
            return false;
        }
        sent += static_cast<size_t>(count);
    }
    char buffer[4096];
    int count = 0;
    while ((count = recv(socket_fd, buffer, sizeof(buffer), 0)) > 0)
        response.append(buffer, static_cast<size_t>(count));
    closeSocket(socket_fd);
    const auto header_end = response.find("\r\n\r\n");
    const auto status_end = response.find(' ');
    if (header_end == std::string::npos || status_end == std::string::npos ||
        response.compare(status_end + 1, 3, "200") != 0) {
        error = "Library server returned an error";
        return false;
    }
    response.erase(0, header_end + 4);
    return true;
}

std::string value(const std::string& object, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    const auto start = object.find(marker);
    if (start == std::string::npos) return {};
    size_t cursor = start + marker.size();
    while (cursor < object.size() &&
           std::isspace(static_cast<unsigned char>(object[cursor]))) ++cursor;
    if (cursor >= object.size() || object[cursor] != '"') return {};
    std::string result;
    for (++cursor; cursor < object.size(); ++cursor) {
        if (object[cursor] == '"' && object[cursor - 1] != '\\') break;
        if (object[cursor] == '\\' && cursor + 1 < object.size()) ++cursor;
        result += object[cursor];
    }
    return result;
}

long long number(const std::string& object, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    const auto start = object.find(marker);
    if (start == std::string::npos) return 0;
    size_t cursor = start + marker.size();
    while (cursor < object.size() &&
           std::isspace(static_cast<unsigned char>(object[cursor]))) ++cursor;
    size_t end = cursor;
    while (end < object.size() &&
           (std::isdigit(static_cast<unsigned char>(object[end])) ||
            object[end] == '-')) ++end;
    try { return std::stoll(object.substr(cursor, end - cursor)); }
    catch (const std::exception&) { return 0; }
}

bool boolean(const std::string& object, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    const auto start = object.find(marker);
    if (start == std::string::npos) return false;
    return object.find("true", start + marker.size()) <
           object.find(',', start + marker.size());
}

bool parseGames(const std::string& json, std::vector<RemoteGame>& games,
                std::string& error) {
    games.clear();
    size_t cursor = 0;
    while ((cursor = json.find('{', cursor)) != std::string::npos) {
        const auto end = json.find('}', cursor);
        if (end == std::string::npos) {
            error = "Invalid game-list JSON";
            return false;
        }
        const std::string object = json.substr(cursor, end - cursor + 1);
        RemoteGame game;
        game.id = static_cast<int>(number(object, "id"));
        game.path = value(object, "path");
        game.title = value(object, "title");
        game.system = value(object, "system");
        game.extension = value(object, "extension");
        game.artwork = value(object, "artwork");
        game.favorite = boolean(object, "favorite");
        game.last_played = number(object, "last_played");
        game.play_count = static_cast<int>(number(object, "play_count"));
        game.tags = value(object, "tags");
        if (!game.path.empty()) games.push_back(std::move(game));
        cursor = end + 1;
    }
    return true;
}

std::string urlEncode(const std::string& text) {
    std::string encoded;
    for (const unsigned char character : text) {
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.') {
            encoded += static_cast<char>(character);
        } else {
            constexpr char hex[] = "0123456789ABCDEF";
            encoded += '%';
            encoded += hex[character >> 4];
            encoded += hex[character & 15];
        }
    }
    return encoded;
}

std::string escapeJson(const std::string& text) {
    std::string escaped;
    for (const char character : text) {
        if (character == '\\' || character == '"') escaped += '\\';
        escaped += character;
    }
    return escaped;
}

} // namespace

LibraryClient::LibraryClient(std::string server_url)
    : server_url_(std::move(server_url)) {}

bool LibraryClient::scan(const std::string& folder, std::string& error) {
    std::string response;
    return request(server_url_, "POST", "/scan",
                   "{\"folder\":\"" + escapeJson(folder) + "\"}",
                   response, error);
}

bool LibraryClient::listGames(std::vector<RemoteGame>& games, std::string& error) {
    std::string response;
    return request(server_url_, "GET", "/games", {}, response, error) &&
           parseGames(response, games, error);
}

bool LibraryClient::listFavorites(std::vector<RemoteGame>& games,
                                   std::string& error) {
    std::string response;
    return request(server_url_, "GET", "/games?filter=favorites", {}, response, error) &&
           parseGames(response, games, error);
}

bool LibraryClient::listRecentlyPlayed(std::vector<RemoteGame>& games,
                                       std::string& error) {
    std::string response;
    return request(server_url_, "GET", "/games?filter=recent", {}, response, error) &&
           parseGames(response, games, error);
}

bool LibraryClient::setFavorite(int id, bool favorite, std::string& error) {
    std::string response;
    return request(server_url_, "POST",
                   "/games/" + std::to_string(id) + "/favorite",
                   std::string("{\"favorite\":") +
                       (favorite ? "true}" : "false}"),
                   response, error);
}

bool LibraryClient::markPlayed(int id, std::string& error) {
    std::string response;
    return request(server_url_, "POST",
                   "/games/" + std::to_string(id) + "/played",
                   {}, response, error);
}

bool LibraryClient::getStatistics(LibraryStatistics& statistics,
                                  std::string& error) {
    std::string response;
    if (!request(server_url_, "GET", "/statistics", {}, response, error))
        return false;
    statistics.total = static_cast<int>(number(response, "total"));
    statistics.favorites = static_cast<int>(number(response, "favorites"));
    statistics.recently_played =
        static_cast<int>(number(response, "recently_played"));
    statistics.plays = static_cast<int>(number(response, "plays"));
    return true;
}

bool LibraryClient::searchCatalog(const std::string& query,
                                  std::vector<CatalogGame>& games,
                                  std::string& error) {
    std::string response;
    if (!request(server_url_, "GET",
                 "/catalog/search?q=" + urlEncode(query), {}, response, error))
        return false;
    games.clear();
    size_t cursor = 0;
    while ((cursor = response.find('{', cursor)) != std::string::npos) {
        const auto end = response.find('}', cursor);
        if (end == std::string::npos) {
            error = "Invalid catalog JSON";
            return false;
        }
        const std::string object = response.substr(cursor, end - cursor + 1);
        CatalogGame game;
        game.id = static_cast<int>(number(object, "id"));
        game.title = value(object, "title");
        game.overview = value(object, "overview");
        game.release_date = value(object, "release_date");
        game.platform = value(object, "platform");
        game.artwork = value(object, "artwork");
        if (!game.title.empty()) games.push_back(std::move(game));
        cursor = end + 1;
    }
    return true;
}

} // namespace emu::frontend
