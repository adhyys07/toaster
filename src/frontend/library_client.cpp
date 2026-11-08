#include "library_client.h"

#include <curl/curl.h>

#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace emu::frontend {
namespace {

void ensureCurlReady() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t appendBody(void* chunk, size_t size, size_t count, void* user) {
    const size_t bytes = size * count;
    static_cast<std::string*>(user)->append(static_cast<char*>(chunk), bytes);
    return bytes;
}

bool request(const std::string& base_url, const std::string& method,
             const std::string& path, const std::string& body,
             std::string& response, std::string& error) {
    if (base_url.rfind("http://", 0) != 0 &&
        base_url.rfind("https://", 0) != 0) {
        error = "Library server URL must start with http:// or https://";
        return false;
    }

    ensureCurlReady();
    CURL* handle = curl_easy_init();
    if (!handle) {
        error = "Could not initialise the HTTP client";
        return false;
    }

    std::string url = base_url;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += path;

    curl_slist* headers =
        curl_slist_append(nullptr, "Content-Type: application/json");

    response.clear();
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "Toaster/0.2");
    // Certificate verification stays on: it is the whole point of https.
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);

    if (!body.empty()) {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(body.size()));
    }

    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);

    if (result != CURLE_OK) {
        error = curl_easy_strerror(result);
        return false;
    }
    if (status < 200 || status >= 300) {
        error = "Library server returned HTTP " + std::to_string(status);
        return false;
    }
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
        const int raw = static_cast<int>(number(object, "compat"));
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
        game.seconds_played =
            static_cast<long long>(number(object, "seconds_played"));
        game.compat = (raw >= 0 && raw <= 5) ? static_cast<Compat>(raw)
                                             : Compat::Unknown;
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

bool LibraryClient::addPlayTime(int id, long long seconds,
                                std::string& error) {
    std::string response;
    return request(server_url_, "POST",
                   "/games/" + std::to_string(id) + "/playtime",
                   "{\"seconds\":" + std::to_string(seconds) + "}",
                   response, error);
}

bool LibraryClient::setTags(int id, const std::vector<std::string>& tags,
                            std::string& error) {
    // Tags come from a text field, so a quote or a backslash in one would
    // otherwise produce a body the server rejects as malformed.
    std::string body = "{\"tags\":[";
    bool first = true;
    for (const std::string& tag : tags) {
        if (tag.empty()) continue;
        if (!first) body += ',';
        first = false;
        body += '"';
        for (const char c : tag) {
            if (c == '"' || c == '\\') body += '\\';
            if (static_cast<unsigned char>(c) < 0x20) continue;
            body += c;
        }
        body += '"';
    }
    body += "]}";

    std::string response;
    return request(server_url_, "PUT",
                   "/games/" + std::to_string(id) + "/tags",
                   body, response, error);
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
