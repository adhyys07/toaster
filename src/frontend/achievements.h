#pragma once

#include "core/system.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace emu::frontend {

std::string achievementHash(const std::string& system_name,
                            const std::string& rom_path,
                            std::string& error);

struct Condition
{
    enum class Source { Memory, Value, Delta };
    enum class Op { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

    Source left_source = Source::Memory;
    uint32_t left = 0;
    Op op = Op::Equal;
    Source right_source = Source::Value;
    uint32_t right = 0;
    uint8_t size = 1;            
    int hit_target = 0;        
    int hits = 0;
};


struct Achievement
{
    uint32_t id = 0;
    std::string title;
    std::string description;
    int points = 0;
    std::vector<Condition> conditions;
    bool unlocked = false;
    bool triggered_this_session = false;
};

class AchievementBackend {
public:
    virtual ~AchievementBackend() = default;
    virtual bool login(const std::string& user, const std::string& token,
                       std::string& error) = 0;
    virtual bool fetch(const std::string& hash,
                       std::vector<Achievement>& out,
                       std::string& error) = 0;
    virtual bool award(uint32_t achievement_id, std::string& error) = 0;
};

class Achievements {
public:
    void setBackend(AchievementBackend* backend) { backend_ = backend; }

    bool load(const std::string& system_name, const std::string& rom_path,
              std::string& error);

    void update(const System& system);

    void setUnlockCallback(std::function<void(const Achievement&)> callback) {
        on_unlock_ = std::move(callback);
    }

    const std::vector<Achievement>& list() const { return achievements_; }
    int unlockedCount() const;
    int totalPoints() const;

    void setHardcore(bool on) { hardcore_ = on; }
    bool hardcore() const { return hardcore_; }

private:
    uint32_t read(const System7 system, uint32_t address, uint8_t size) const;
    bool evaluate(const System& system, Achievement& achievement);

    AchievementBackend* backend_ = nullptr;
    std::vector<Achievement> achievements_;
    std::function<void(const Achievement&)> on_unlock_;
    bool hardcore_ = false;
};

}