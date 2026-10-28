#pragma once

#include <string>

namespace audiovcs {
namespace hash {

// Compute the SHA-256 hash of a file.
// Returns the hex-encoded digest, or empty string on error.
std::string file(const std::string& path);

} // namespace hash
} // namespace audiovcs
