#include "movie.h"

#include <cstring>
#include <fstream>

namespace emu::frontend {

namespace {

constexpr char kMagic[4] = {'T', 'S', 'M', 'V'};
constexpr uint32_t kVersion = 1;

// Four bytes per entry, so this caps a replay at about 256 MB of input -
// far beyond any real session, and small enough that a corrupt header
// cannot exhaust memory.
constexpr unsigned long long kMaxInputs = 64ull * 1024ull * 1024ull;

void put32(std::ostream& out, uint32_t value) {
    char bytes[4];
    bytes[0] = static_cast<char>(value & 0xFF);
    bytes[1] = static_cast<char>((value >> 8) & 0xFF);
    bytes[2] = static_cast<char>((value >> 16) & 0xFF);
    bytes[3] = static_cast<char>((value >> 24) & 0xFF);
    out.write(bytes, 4);
}

bool get32(std::istream& in, uint32_t& value) {
    unsigned char bytes[4];
    if (!in.read(reinterpret_cast<char*>(bytes), 4)) return false;
    value = static_cast<uint32_t>(bytes[0]) |
            (static_cast<uint32_t>(bytes[1]) << 8) |
            (static_cast<uint32_t>(bytes[2]) << 16) |
            (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

}

bool Movie::startRecording(const System& system, std::string& error) {
    stop();
    if (!system.saveState(initial_state_) || initial_state_.empty()) {
        error = "This core has no save states, so a replay could not be "
                "started from a known point.";
        return false;
    }
    ports_ = system.portCount() > 0 ? system.portCount() : 1;
    core_name_ = system.name() ? system.name() : "";
    frames_.clear();
    cursor_ = 0;
    mode_ = Mode::Recording;
    error.clear();
    return true;
}

void Movie::record(const uint32_t* inputs, int ports) {
    if (mode_ != Mode::Recording || !inputs) return;
    for (int i = 0; i < ports_; ++i) {
        frames_.push_back(i < ports ? inputs[i] : 0u);
    }
}

bool Movie::save(const std::filesystem::path& file, const System& system,
                 std::string& error) const {
    if (frames_.empty()) {
        error = "Nothing was recorded.";
        return false;
    }
    (void)system;

    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
    }

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Could not create " + file.string();
        return false;
    }
    out.write(kMagic, 4);
    put32(out, kVersion);
    put32(out, static_cast<uint32_t>(ports_));
    put32(out, static_cast<uint32_t>(frames_.size() / ports_));
    put32(out, static_cast<uint32_t>(core_name_.size()));
    out.write(core_name_.data(),
              static_cast<std::streamsize>(core_name_.size()));
    put32(out, static_cast<uint32_t>(initial_state_.size()));
    out.write(reinterpret_cast<const char*>(initial_state_.data()),
              static_cast<std::streamsize>(initial_state_.size()));
    for (uint32_t frame : frames_) put32(out, frame);

    if (!out.good()) {
        error = "Could not finish writing " + file.string();
        return false;
    }
    error.clear();
    return true;
}

bool Movie::startReplay(const std::filesystem::path& file, System& system,
                        std::string& error) {
    stop();

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "Could not open " + file.string();
        return false;
    }

    char magic[4] = {};
    if (!in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0) {
        error = "That is not a Toaster movie file.";
        return false;
    }

    uint32_t version = 0;
    uint32_t ports = 0;
    uint32_t frame_count = 0;
    uint32_t name_length = 0;
    if (!get32(in, version) || !get32(in, ports) || !get32(in, frame_count) ||
        !get32(in, name_length)) {
        error = "The movie header is truncated.";
        return false;
        }
    if (version != kVersion) {
        error = "That movie was written by a different version of Toaster.";
        return false;
    }

    std::string name(name_length, '\0');
    if (name_length && !in.read(name.data(), name_length)) {
        error = "The movie header is truncated.";
        return false;
    }

    const std::string current = system.name() ? system.name() : "";
    if (!name.empty() && name != current) {
        error = "That movie was recorded on " + name + ", not " + current + ".";
        return false;
    }

    uint32_t state_length = 0;
    if (!get32(in, state_length)) {
        error = "The movie header is truncated.";
        return false;
    }
    initial_state_.assign(state_length, 0);
    if (state_length &&
        !in.read(reinterpret_cast<char*>(initial_state_.data()),
                 state_length)) {
        error = "The movie's initial state is truncated.";
        return false;
    }

    // A corrupt header could claim billions of frames. The product is
    // computed in 64 bits and bounded before anything is reserved, so a bad
    // file is rejected rather than attempting the allocation it asks for.
    const unsigned long long claimed =
        static_cast<unsigned long long>(frame_count) * ports;
    if (claimed > kMaxInputs) {
        error = "That movie claims more input than Toaster can hold.";
        return false;
    }

    frames_.clear();
    frames_.reserve(static_cast<std::size_t>(claimed));
    for (unsigned long long i = 0; i < claimed; ++i) {
        uint32_t value = 0;
        if (!get32(in, value)) {
            error = "The movie's input log is truncated.";
            return false;
        }
        frames_.push_back(value);
    }

    if (!system.loadState(initial_state_)) {
        error = "The core rejected the movie's initial state.";
        return false;
    }

    ports_ = static_cast<int>(ports > 0 ? ports : 1);
    cursor_ = 0;
    mode_ = Mode::Replaying;
    error.clear();
    return true;
}

bool Movie::next(uint32_t* inputs, int ports) {
    if (mode_ != Mode::Replaying || !inputs) return false;
    if ((cursor_ + 1) * ports_ > frames_.size()) {
        stop();
        return false;
    }
    for (int i = 0; i < ports; ++i) {
        inputs[i] = i < ports_ ? frames_[cursor_ * ports_ + i] : 0u;
    }
    ++cursor_;
    return true;
}

void Movie::stop() {
    mode_ = Mode::Idle;
    cursor_ = 0;
    frames_.clear();
    initial_state_.clear();
}

}