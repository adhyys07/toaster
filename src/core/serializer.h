#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace emu {

constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kStateMagic = 0x54535441u;

class StateWriter {
public:
    template <typename T>
    void operator()(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "state fields must be trivially copyable");
        const auto* begin = reinterpret_cast<const uint8_t*>(&value);
        data_.insert(data_.end(), begin, begin + sizeof(T));
    }

    void block(const void* source, size_t size) {
        const auto* begin = static_cast<const uint8_t*>(source);
        data_.insert(data_.end(), begin, begin + size);
    }

    static constexpr bool saving = true;
    bool ok() const { return true; }
    std::vector<uint8_t> take() { return std::move(data_); }

private:
    std::vector<uint8_t> data_;
};

class StateReader {
public:
    StateReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    template <typename T>
    void operator()(T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "state fields must be trivially copyable");
        if (!advance(sizeof(T))) return;
        std::memcpy(&value, data_ + cursor_ - sizeof(T), sizeof(T));
    }

    void block(void* destination, size_t size) {
        if (!advance(size)) return;
        std::memcpy(destination, data_ + cursor_ - size, size);
    }

    static constexpr bool saving = false;
    bool ok() const { return ok_; }
    bool exhausted() const { return ok_ && cursor_ == size_; }


private:
    bool advance(size_t size) {
        // Phrased as a subtraction (cursor_ <= size_ always holds) so a
        // bogus size cannot overflow past the end check.
        if (!ok_ || size > size_ - cursor_) {
            ok_ = false;
            return false;
        }
        cursor_ += size;
        return true;
    }

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t cursor_ = 0;
    bool ok_ = true;
};

}  // namespace emu
