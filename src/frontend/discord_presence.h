#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend {

class SaveSync {
public:
    explicit SaveSync(std::string server_url);

    struct Entry {
        std::string kind;
        std::string digest;
        long long modified = 0;
    };

    enum class Result { UpToDate, Uploaded, Downloaded, Conflict, Failed };

    bool list(int game_id, std::vector<Entry>& out, std::string& error);

    Result sync(int game_id, const std::string& kind,
                const std::filesystem::path& local, std::string& error);

    bool upload(int game_id, const std::string& kind,
                const std::filesystem::path& local, std::string& error);

    bool download(int game_id, const std::string& kind,
                  const std::filesystem::path& local, std::string& error);

private:
    std::string server_;
};

std::string sha256File(const std::filesystem::path& file);

} // namespace emu::frontend
