#pragma once

#include "core/system.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace emu::frontend {

class Movie {
public:
    enum class Mode { Idle, Recording, Replaying };

    Mode mode() const { return mode_; }
    std::size_t frameCount() const { return frames_.size() / ports_; }
    std::size_t position() const { return cursor_; }

    bool startRecording(const System& system, std::string& error);
    void record(const uint32_t* inputs, int ports);

    bool save(const std::filesystem::path& file, const System& system,
              std::string& error) const;

    bool startReplay(const std::filesystem::path& file, System& system,
                     std::string& error);

    bool next(uint32_t* inputs, int ports);

    void stop();

private:
    Mode mode_ = Mode::Idle;
    int ports_ = 1;
    std::size_t cursor_ = 0;
    std::vector<uint32_t> frames_;
    std::vector<uint8_t> initial_state_;
    std::string core_name_;
};

}