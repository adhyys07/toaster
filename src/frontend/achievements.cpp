#include "achievements.h"

#include "sha256.h"

#include <cstdio>
#include <fstream>

namespace emu::frontend {
namespace {
constexpr std::size_t kINesHeaderSize = 16;

bool readWholeFile(const std::string& path, std::vector<unsigned char>& out,
                   std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Could not open " + path;
        return false;
    }

    const std::streamoff size = file.tellg();
    if (size <= 0) {
        error = path + " is empty.";
        return false;
    }
    file.seekg(0, std::ios::beg);

    out.assign(static_cast<std::size_t>(size), 0);
    if (!file.read(reinterpret_cast<char*>(out.data()), size)) {
        error = "Could not read " + path;
        return false;
    }
    return true;
}

} // namespace

std::string achievementHash(const std::string& system_name,
                            const std::string& rom_path,
                            std::string& error) {
    std::vector<unsigned char> bytes;
    if (!readWholeFile(rom_path, bytes, error)) return std::string();

    std::size_t offset = 0;
    std::size_t length = bytes.size();

    if (system_name == "NES") {
        // "NES\x1a" marks a headered dump. A headerless one hashes whole,
        // which is also what the database holds for those.
        const bool headered =
            bytes.size() > kINesHeaderSize && bytes[0] == 'N' &&
            bytes[1] == 'E' && bytes[2] == 'S' && bytes[3] == 0x1A;
        if (headered) {
            offset = kINesHeaderSize;
            length = bytes.size() - kINesHeaderSize;
        }
    }

    detail::Sha256Context context;
    detail::sha256Update(context, bytes.data() + offset, length);
    error.clear();
    return detail::sha256Finish(context);
}

bool Achievements::load(const std::string& system_name,
                        const std::string& rom_path, std::string& error) {
    achievements_.clear();

    const std::string hash = achievementHash(system_name, rom_path, error);
    if (hash.empty()) return false;

    if (!backend_) {
        // No backend is the normal offline case, not a failure: the hash is
        // still worth computing so the UI can show it.
        error.clear();
        return true;
    }

    if (!backend_->fetch(hash, achievements_, error)) {
        achievements_.clear();
        return false;
    }

    // A fresh session has earned nothing yet, whatever the server said about
    // past unlocks, so the per-session flag starts clear and `unlocked`
    // carries only what the server reported.
    for (Achievement& achievement : achievements_) {
        achievement.triggered_this_session = false;
        for (Condition& condition : achievement.conditions) {
            condition.hits = 0;
        }
    }

    error.clear();
    return true;
}

uint32_t Achievements::read(const System& system, uint32_t address,
                            uint8_t size) const {
    uint8_t low = 0;
    if (!system.readMemory(address, low)) return 0;
    if (size < 2) return low;

    uint8_t high = 0;
    // A word that runs off the end of a region reads as just its low byte
    // rather than as zero, which would make a "greater than" test flip.
    if (!system.readMemory(address + 1, high)) return low;
    return static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 8);
}

bool Achievements::evaluate(const System& system, Achievement& achievement) {
    bool all = true;

    for (Condition& condition : achievement.conditions) {
        const uint32_t left =
            condition.left_source == Condition::Source::Value
                ? condition.left
                : read(system, condition.left, condition.size);
        const uint32_t right =
            condition.right_source == Condition::Source::Value
                ? condition.right
                : read(system, condition.right, condition.size);

        bool satisfied = false;
        switch (condition.op) {
        case Condition::Op::Equal:        satisfied = left == right; break;
        case Condition::Op::NotEqual:     satisfied = left != right; break;
        case Condition::Op::Less:         satisfied = left < right; break;
        case Condition::Op::LessEqual:    satisfied = left <= right; break;
        case Condition::Op::Greater:      satisfied = left > right; break;
        case Condition::Op::GreaterEqual: satisfied = left >= right; break;
        }

        // A hit target counts the frames a condition held, not consecutive
        // frames: that is how "collect 50 coins" survives a counter that
        // flickers between two values while the HUD animates.
        if (condition.hit_target > 0) {
            if (satisfied) ++condition.hits;
            satisfied = condition.hits >= condition.hit_target;
        }

        if (!satisfied) all = false;
    }

    return all;
}

void Achievements::update(const System& system) {
    for (Achievement& achievement : achievements_) {
        if (achievement.unlocked || achievement.triggered_this_session) {
            // Still skipped rather than evaluated: a satisfied trigger would
            // otherwise keep firing every frame for the rest of the session.
            continue;
        }
        if (!evaluate(system, achievement)) continue;

        achievement.triggered_this_session = true;
        achievement.unlocked = true;
        if (on_unlock_) on_unlock_(achievement);

        if (!backend_) continue;

        std::string error;
        if (!backend_->award(achievement.id, error)) {
            // Logged once and not retried: retrying every frame would hammer
            // the server, and the unlock is already shown to the player.
            std::fprintf(stderr, "Could not award achievement %u: %s\n",
                         achievement.id, error.c_str());
        }
    }
}

int Achievements::unlockedCount() const {
    int count = 0;
    for (const Achievement& achievement : achievements_) {
        if (achievement.unlocked) ++count;
    }
    return count;
}

int Achievements::totalPoints() const {
    int points = 0;
    for (const Achievement& achievement : achievements_) {
        if (achievement.unlocked) points += achievement.points;
    }
    return points;
}

} // namespace emu::frontend
