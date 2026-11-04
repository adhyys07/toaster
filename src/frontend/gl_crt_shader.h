#pragma once

#include <memory>
#include <string>

#include <SDL.h>

#include "core/system.h"

namespace emu::frontend {

class CrtShader {
public:
    CrtShader();
    ~CrtShader();

    CrtShader(const CrtShader&) = delete;
    CrtShader& operator=(const CrtShader&) = delete;

    bool initialize(SDL_Window* window,
                    const std::string& shader_directory,
                    std::string& error);
    bool render(const emu::VideoFrame& frame,
                double aspect_ratio,
                float sharpness,
                std::string& error);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emu::frontend
