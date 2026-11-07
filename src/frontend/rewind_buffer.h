#pragma once

#include "core/system.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace emu::frontend {

class RewindBuffer {
public:
    RewindBuffer(size_t max_states = 600, unsigned interval = 2)
        :max_states_(max_states), interval_(interval) {}

    void clear() { states_.clear(); frame_ = 0; bytes_ = 0; }
    bool empty() const { return states_.empty(); }
    size_t depth() const { return states_.size(); }
    size_t bytes() const { return bytes_; }
    size_t capacity() const { return max_states_; }
    unsigned interval() const { return interval_; }

    float seconds() const {
        if (interval_ == 0) return 0.0f;
        return static_cast<float>(states_.size() * interval_) / 60.0f;
    }


    void capture(const System& system) {
        if (max_states_ == 0 || interval_ == 0) return;
        if ((frame_++ % interval_) != 0) return;
        std::vector<uint8_t> state;
        if (!system.saveState(state) || state.empty()) return;
        bytes_ += state.size();
        states_.push_back(std::move(state));
        while (states_.size() > max_states_) {
            bytes_ -= states_.front().size();
            states_.pop_front();
        }
    }

    bool rewind(System& system) {
        while (!states_.empty()) {
            std::vector<uint8_t> state = std::move(states_.back());
            bytes_ -= state.size();
            states_.pop_back();
            if (system.loadState(state)) return true;
        }

        return false;
    }

private:
    std::deque<std::vector<uint8_t>> states_;
    size_t max_states_;
    unsigned interval_;
    unsigned frame_ = 0;
    size_t bytes_ = 0;
};
}