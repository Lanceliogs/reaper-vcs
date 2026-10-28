#pragma once

#include <wavpack.h>

#include <cstdint>
#include <string>

namespace audiovcs {
namespace wav {

struct SplitInfo {
    // Bytes [0, data_offset) = header wrapper
    // (RIFF header + fmt + pre-data chunks + "data" chunk header)
    uint64_t data_offset;

    // Bytes [data_offset, data_offset + data_size) = PCM samples
    uint64_t data_size;

    // Bytes [trailer_offset, file_size) = trailing wrapper
    // (post-data chunks, trailing junk, etc.)
    uint64_t trailer_offset;

    uint64_t file_size;

    // Audio format info, populated from the fmt chunk
    WavpackConfig config;
    int block_align;
};

// Parse a WAV file and locate the boundaries needed for wrapper-based
// compression. Returns false on parse error or non-WAV input.
bool analyse(const std::string& path, SplitInfo& info);

} // namespace wav
} // namespace audiovcs
