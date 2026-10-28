#pragma once

#include <cstdint>
#include <iostream>
#include <string>

namespace audiovcs {
namespace pointer {

constexpr const char* VERSION_LINE = "version audiovcs/1";

struct Pointer {
    std::string hash;   // hex-encoded SHA-256
    uint64_t size;      // original file size in bytes
};

// Check whether a buffer looks like a pointer file rather than audio data.
bool is_pointer(const void* data, size_t len);

// Parse a pointer from a stream. Returns false on malformed input.
bool read(std::istream& in, Pointer& ptr);

// Write a pointer to a stream.
bool write(std::ostream& out, const Pointer& ptr);

} // namespace pointer
} // namespace audiovcs
