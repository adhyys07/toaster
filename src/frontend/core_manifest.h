#pragma once

#include <istream>
#include <map>
#include <string>

namespace emu::frontend {

// A core server's manifest maps each core filename to the SHA-256 it must
// hash to. Cores are loaded as native code, so this is what stands between a
// tampered download and LoadLibrary.
using CoreManifest = std::map<std::string, std::string>;

// Parses "<sha256> <filename>" lines. Entries that cannot verify anything are
// dropped rather than trusted, so a malformed manifest fails closed:
//
//   - a digest that is not 64 hex characters
//   - a filename containing a path separator or "..", which would let a
//     manifest write outside the cores directory
//
// Two input quirks are tolerated, because both are what the obvious command
// produces: sha256sum defaults to binary mode on Windows and writes
// "<hash> *name", and a manifest edited on Windows has CRLF line endings.
CoreManifest parseCoreManifest(std::istream& input);

} // namespace emu::frontend
