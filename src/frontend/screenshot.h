#pragma once

#include "core/system.h"

#include <string>

namespace emu::frontend {

// Writes the frame to screenshots/<game>-<timestamp>-<n>.png beside the
// executable. Returns the path written, or an empty string on failure with
// `error` filled in.
std::string saveScreenshot(const VideoFrame& frame,
                           const std::string& game_path,
                           std::string& error);

} // namespace emu::frontend
