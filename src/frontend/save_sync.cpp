#include "save_sync.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>
#include <utility>

namespace emu::frontend {
namespace {

std::size_t appendBody(char* data, std::size_t size, std::size_t count,
                       void* user) {
    auto* out = static_cast<std::string*>(user);
    out->append(data, size * count);
    return size * count;
}

std::size_t readBody(char* buffer, std::size_t size, std::size_t count,
                     void* user) {
    auto* cursor = static_cast<std::pair<const std::string*, std::size_t>*>(
        user);
    const std::string& body = *cursor->first;
    const std::size_t remaining = body.size() - cursor->second;
    const std::size_t take = std::min(remaining, size * count);
    std::memcpy(buffer, body.data() + cursor->second, take);
    cursor->second += take;
    return take;
}

CURL* newHandle(const std::string& url, std::string& response) {
    CURL* handle = curl_easy_init();
    if (!handle) return nullptr;

    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "Toaster/0.2");
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    return handle;
}

bool perform(CURL* handle, long& status, std::string& error) {
    const CURLcode result = curl_easy_perform(handle);
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK) {
        error = curl_easy_strerror(result);
        return false;
    }
    if (status < 200 || status >= 300) {
        error = "The server returned HTTP " + std::to_string(status) + ".";
        return false;
    }
    return true;
}

std::string jsonString(const std::string& body, const std::string& key,
                       std::size_t from, std::size_t* next) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t at = body.find(needle, from);
    if (at == std::string::npos) return std::string();
    const std::size_t colon = body.find(':', at + needle.size());
    if (colon == std::string::npos) return std::string();
    const std::size_t open = body.find('"', colon);
    if (open == std::string::npos) return std::string();
    const std::size_t close = body.find('"', open + 1);
    if (close == std::string::npos) return std::string();
    if (next) *next = close;
    return body.substr(open + 1, close - open - 1);
}

long long jsonNumber(const std::string& body, const std::string& key,
                     std::size_t from) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t at = body.find(needle, from);
    if (at == std::string::npos) return 0;
    const std::size_t colon = body.find(':', at + needle.size());
    if (colon == std::string::npos) return 0;
    try {
        return std::stoll(body.substr(colon + 1, 32));
    } catch (...) {
        return 0;
    }
}

bool readFileBytes(const std::filesystem::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    if (size < 0) return false;
    file.seekg(0, std::ios::beg);
    out.assign(static_cast<std::size_t>(size), '\0');
    if (size > 0 && !file.read(out.data(), size)) return false;
    return true;
}

} // namespace

SaveSync::SaveSync(std::string server_url) : server_(std::move(server_url)) {
    while (!server_.empty() && server_.back() == '/') server_.pop_back();
}

bool SaveSync::list(int game_id, std::vector<Entry>& out,
                    std::string& error) {
    out.clear();
    if (server_.empty()) {
        error = "No library server is configured.";
        return false;
    }

    const std::string url =
        server_ + "/saves/" + std::to_string(game_id);
    std::string response;
    CURL* handle = newHandle(url, response);
    if (!handle) {
        error = "Could not start an HTTP request.";
        return false;
    }

    long status = 0;
    const bool ok = perform(handle, status, error);
    curl_easy_cleanup(handle);
    if (!ok) return false;

    std::size_t cursor = 0;
    while (true) {
        std::size_t next = 0;
        const std::string kind = jsonString(response, "kind", cursor, &next);
        if (kind.empty()) break;
        Entry entry;
        entry.kind = kind;
        entry.digest = jsonString(response, "digest", next, nullptr);
        entry.modified = jsonNumber(response, "modified", next);
        out.push_back(std::move(entry));
        cursor = next + 1;
    }

    error.clear();
    return true;
}

bool SaveSync::upload(int game_id, const std::string& kind,
                      const std::filesystem::path& local,
                      std::string& error) {
    if (server_.empty()) {
        error = "No library server is configured.";
        return false;
    }

    std::string body;
    if (!readFileBytes(local, body)) {
        error = "Could not read " + local.string();
        return false;
    }

    const std::string url =
        server_ + "/saves/" + std::to_string(game_id) + "/" + kind;
    std::string response;
    CURL* handle = newHandle(url, response);
    if (!handle) {
        error = "Could not start an HTTP request.";
        return false;
    }

    std::pair<const std::string*, std::size_t> cursor{&body, 0};
    curl_slist* headers =
        curl_slist_append(nullptr, "Content-Type: application/octet-stream");
    curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_READFUNCTION, readBody);
    curl_easy_setopt(handle, CURLOPT_READDATA, &cursor);
    curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE,
                     static_cast<curl_off_t>(body.size()));

    long status = 0;
    const bool ok = perform(handle, status, error);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    return ok;
}

bool SaveSync::download(int game_id, const std::string& kind,
                        const std::filesystem::path& local,
                        std::string& error) {
    if (server_.empty()) {
        error = "No library server is configured.";
        return false;
    }

    const std::string url =
        server_ + "/saves/" + std::to_string(game_id) + "/" + kind;
    std::string response;
    CURL* handle = newHandle(url, response);
    if (!handle) {
        error = "Could not start an HTTP request.";
        return false;
    }

    long status = 0;
    const bool ok = perform(handle, status, error);
    curl_easy_cleanup(handle);
    if (!ok) return false;

    std::error_code ec;
    if (local.has_parent_path()) {
        std::filesystem::create_directories(local.parent_path(), ec);
    }

    std::filesystem::path temporary = local;
    temporary += ".part";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "Could not write " + temporary.string();
            return false;
        }
        file.write(response.data(),
                   static_cast<std::streamsize>(response.size()));
        if (!file.good()) {
            error = "Could not finish writing " + temporary.string();
            return false;
        }
    }

    std::filesystem::rename(temporary, local, ec);
    if (ec) {
        std::filesystem::remove(local, ec);
        std::filesystem::rename(temporary, local, ec);
        if (ec) {
            error = "Could not replace " + local.string() + ": " +
                    ec.message();
            return false;
        }
    }

    error.clear();
    return true;
}

SaveSync::Result SaveSync::sync(int game_id, const std::string& kind,
                                const std::filesystem::path& local,
                                std::string& error) {
    std::vector<Entry> remote;
    if (!list(game_id, remote, error)) return Result::Failed;

    const Entry* match = nullptr;
    for (const Entry& entry : remote) {
        if (entry.kind == kind) {
            match = &entry;
            break;
        }
    }

    std::error_code ec;
    const bool have_local = std::filesystem::is_regular_file(local, ec);

    if (!match) {
        if (!have_local) {
            error.clear();
            return Result::UpToDate;
        }
        return upload(game_id, kind, local, error) ? Result::Uploaded
                                                   : Result::Failed;
    }

    if (!have_local) {
        return download(game_id, kind, local, error) ? Result::Downloaded
                                                     : Result::Failed;
    }

    const std::string digest = sha256File(local);
    if (!digest.empty() && digest == match->digest) {
        error.clear();
        return Result::UpToDate;
    }

    error = "The local and remote saves for this game have both changed.";
    return Result::Conflict;
}

} // namespace emu::frontend
