#include "memory_scan.h"

namespace emu::frontend {
    
const char* compareName(MemoryScan::Compare compare) {
    switch (compare) {
    case MemoryScan::Compare::Equal:     return "equal to";
    case MemoryScan::Compare::NotEqual:  return "not equal to";
    case MemoryScan::Compare::Less:      return "less than";
    case MemoryScan::Compare::Greater:   return "greater than";
    case MemoryScan::Compare::Changed:   return "changed";
    case MemoryScan::Compare::Unchanged: return "unchanged";
    case MemoryScan::Compare::Decreased: return "decreased";
    case MemoryScan::Compare::Increased: return "increased";
    default:                             return "";
    }
}

void MemoryScan::reset(Width width) {
    candidates_.clear();
    width_ = width;
    passes_ = 0;
}

bool MemoryScan::read(const System& system, uint32_t address,
                      uint32_t& value) const {
    uint8_t low = 0;
    if (!system.readMemory(address, low)) return false;
    if (width_ == Width::Byte) {
        value = low;
        return true;
    }
    uint8_t high = 0;
    if (!system.readMemory(address + 1, high)) return false;
    // Little-endian: every CPU in this project stores words low byte first.
    value = static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 8);
    return true;
}

bool MemoryScan::first(const System& system) {
    candidates_.clear();
    passes_ = 0;

    const std::vector<System::MemoryRegion> regions = system.memoryRegions();
    if (regions.empty()) return false;

    const uint32_t stride = width_ == Width::Byte ? 1u : 2u;
    std::size_t total = 0;
    for (const System::MemoryRegion& region : regions) total += region.size;
    candidates_.reserve(total / stride);

    for (const System::MemoryRegion& region : regions) {
        const uint32_t end = region.start + region.size -
                             (width_ == Width::Byte ? 0u : 1u);
        for (uint32_t address = region.start; address < end;
             address += stride) {
            uint32_t value = 0;
            if (!read(system, address, value)) continue;
            candidates_.push_back(Candidate{address, value, value});
        }
    }

    passes_ = 1;
    return !candidates_.empty();
}

bool MemoryScan::next(const System& system, Compare compare,
                      uint32_t operand) {
    if (candidates_.empty()) return false;

    std::vector<Candidate> kept;
    kept.reserve(candidates_.size());

    for (Candidate candidate : candidates_) {
        uint32_t value = 0;
        if (!read(system, candidate.address, value)) continue;

        const uint32_t before = candidate.current;
        bool keep = false;
        switch (compare) {
        case Compare::Equal:     keep = value == operand; break;
        case Compare::NotEqual:  keep = value != operand; break;
        case Compare::Less:      keep = value < operand; break;
        case Compare::Greater:   keep = value > operand; break;
        case Compare::Changed:   keep = value != before; break;
        case Compare::Unchanged: keep = value == before; break;
        case Compare::Decreased: keep = value < before; break;
        case Compare::Increased: keep = value > before; break;
        }
        if (!keep) continue;

        candidate.previous = before;
        candidate.current = value;
        kept.push_back(candidate);
    }

    candidates_.swap(kept);
    ++passes_;
    return !candidates_.empty();
}

void MemoryScan::refresh(const System& system) {
    for (Candidate& candidate : candidates_) {
        uint32_t value = 0;
        if (!read(system, candidate.address, value)) continue;
        candidate.previous = candidate.current;
        candidate.current = value;
    }

}

}

