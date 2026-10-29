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
 
class System {
public:
    virtual ~System() = default;
 
    virtual const char* name() const = 0;
 
    virtual std::string romInfo() const { return {}; }
 
    virtual bool loadRom(const std::string& path, std::string& error) = 0;
    virtual void reset() = 0;

    virtual void runFrame() = 0;

    virtual VideoFrame video() const = 0;
    virtual double refreshRate() const = 0;

    virtual double aspectRatio() const {
        const VideoFrame f = video();
        return f.height > 0 ? static_cast<double>(f.width) / f.height : 1.0;
    }

    virtual int sampleRate() const = 0;

    virtual size_t readAudio(float* out, size_t frames) = 0;

    virtual int portCount() const { return 1; }
    virtual const std::vector<ButtonInfo>& buttons() const = 0;
    virtual void setInput(int port, uint32_t state) = 0;
    virtual bool saveState(std::vector<uint8_t>& out) const { (void)out; return false; }
    virtual bool loadState(const std::vector<uint8_t>& in) { (void)in; return false; }
};
 
using SystemPtr = std::unique_ptr<System>;
 
}
