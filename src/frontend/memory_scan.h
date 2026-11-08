#pragma once

#include "core/system.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace emu::frontend {

class MemoryScan {
public:
    enum class Width { Byte, Word };

    enum class Compare {
        Equal,
        NotEqual,
        Less,
        Greater,
        Changed,        // differs from the previous pass
        Unchanged,
        Decreased,
        Increased
    };
    
    struct Candidate
    {
        uint32_t address = 0;
        uint32_t previous = 0;
        uint32_t current = 0;
    };

    void reset(Width width);
    Width width() const { return width_; }
    bool first(const System& system);
    bool next(const System& system, Compare compare, uint32_t operand);

    std::size_t size() const { return candidates_.size(); }
    bool empty() const { return candidates_.empty(); }
    const Candidate& at(std::size_t index) const { return candidates_[index]; }
    int passes() const { return passes_; }

    void refresh(const System& system);
    static constexpr std::size_t kListLimit = 2000;

private:
    bool read(const System& system, uint32_t address, uint32_t& value) const;

    std::vector<Candidate> candidates_;
    Width width_ = Width::Byte;
    int passes_ = 0;
};

const char* compareName(MemoryScan::Compare compare);

}
