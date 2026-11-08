#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emu {
 
struct VideoFrame {
    const uint32_t* pixels = nullptr;
    int width  = 0;
    int height = 0;
};
 
struct ButtonInfo {
    const char* name;         
    uint32_t    mask;         
    const char* default_key; 
};

struct MemoryPatch {
    uint16_t address = 0;
    uint8_t  value = 0;
    bool     has_compare = false;
    uint8_t  compare = 0;
};

struct Overscan {
    int top = 0;
    int bottom = 0;
    int left = 0;
    int right = 0;
};

class System {
public:
    virtual ~System() = default;

    virtual void setPatches(const std::vector<MemoryPatch>& patches) {
        (void)patches;
    }
    virtual bool supportsPatches() const { return false; }
    
    virtual bool readMemory(uint32_t address, uint8_t& value) const {
        (void)address;
        (void)value;
        return false;
    }

    struct MemoryRegion {
        const char* name;
        uint32_t start;
        uint32_t size;
    };

    virtual std::vector<MemoryRegion> memoryRegions() const { return {}; }

    virtual const char* name() const = 0;
 
    virtual std::string romInfo() const { return {}; }
 
    virtual bool loadRom(const std::string& path, std::string& error) = 0;
    virtual void reset() = 0;

    virtual void runFrame() = 0;

    virtual VideoFrame video() const = 0;
    virtual double refreshRate() const = 0;

    virtual Overscan overscan() const { return {}; }

    virtual double aspectRatio() const {
        const VideoFrame f = video();
        return f.height > 0 ? static_cast<double>(f.width) / f.height : 1.0;
    }

    virtual int sampleRate() const = 0;

    virtual size_t readAudio(float* out, size_t frames) = 0;

    virtual int audioChannelCount() const { return 0; }

    virtual const char* audioChannelName(int index) const {
        (void)index;
        return "";
    }

    virtual void setAudioChannelMask(uint32_t mask) { (void)mask; }
    virtual uint32_t audioChannelMask() const { return 0; }


    virtual bool setPalette(const uint32_t* entries, int count) {
        (void)entries;
        (void)count;
        return false;
    }

    virtual int paletteSize() const { return 0; }

    // How many rotated copies of a battery save to keep. A no-op for a
    // core with no battery saves. Set once by the frontend right after
    // construction, so the core never needs to know about Settings.
    virtual void setSaveBackupCount(int keep) { (void)keep; }


    virtual int portCount() const { return 1; }
    virtual const std::vector<ButtonInfo>& buttons() const = 0;
    virtual void setInput(int port, uint32_t state) = 0;
    virtual bool saveState(std::vector<uint8_t>& out) const { (void)out; return false; }
    virtual bool loadState(const std::vector<uint8_t>& in) { (void)in; return false; }

    virtual std::string debugInfo() const { return {}; }
};

using SystemPtr = std::unique_ptr<System>;
 
}
