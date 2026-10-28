#include "audiovcs/wav.h"

#include <array>
#include <cstring>
#include <fstream>
#include <iostream>

namespace audiovcs {
namespace wav {

using FourCC = std::array<char, 4>;

static constexpr FourCC fourCC(const char (&s)[5]) noexcept {
    return {s[0], s[1], s[2], s[3]};
}

static uint16_t readU16LE(std::istream& in) {
    uint8_t b[2];
    in.read(reinterpret_cast<char*>(b), 2);
    return uint16_t(b[0]) | (uint16_t(b[1]) << 8);
}

static uint32_t readU32LE(std::istream& in) {
    uint8_t b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    return uint32_t(b[0])
         | (uint32_t(b[1]) << 8)
         | (uint32_t(b[2]) << 16)
         | (uint32_t(b[3]) << 24);
}

static bool parseFmt(std::istream& in, uint32_t chunk_size,
                     WavpackConfig& cfg, int& block_align)
{
    if (chunk_size < 16)
        return false;

    uint16_t format_tag   = readU16LE(in);
    cfg.num_channels      = readU16LE(in);
    cfg.sample_rate       = readU32LE(in);
    readU32LE(in); // BytesPerSecond (skip)
    block_align           = readU16LE(in);
    cfg.bits_per_sample   = readU16LE(in);
    cfg.bytes_per_sample  = (cfg.bits_per_sample + 7) / 8;

    // IEEE float
    if (format_tag == 3)
        cfg.float_norm_exp = 127;

    // WAVEFORMATEXTENSIBLE
    if (format_tag == 0xFFFE && chunk_size >= 40) {
        readU16LE(in); // cbSize
        readU16LE(in); // ValidBitsPerSample
        cfg.channel_mask = static_cast<int32_t>(readU32LE(in));

        uint16_t sub_format = readU16LE(in);
        if (sub_format == 3)
            cfg.float_norm_exp = 127;
    }

    return in.good();
}

bool analyse(const std::string& path, SplitInfo& info) {
    std::memset(&info, 0, sizeof(info));

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    in.seekg(0, std::ios::end);
    info.file_size = in.tellg();
    in.seekg(0);

    FourCC id;
    in.read(id.data(), 4);
    if (id != fourCC("RIFF"))
        return false;

    uint32_t riff_size = readU32LE(in);

    in.read(id.data(), 4);
    if (id != fourCC("WAVE"))
        return false;

    uint64_t riff_end = uint64_t(riff_size) + 8;
    bool found_fmt  = false;
    bool found_data = false;
    uint64_t offset = 12;

    while (offset + 8 <= riff_end) {
        in.seekg(offset);
        in.read(id.data(), 4);
        if (!in) break;

        uint32_t chunk_size = readU32LE(in);
        uint64_t data_start = offset + 8;
        bool     has_pad    = (chunk_size & 1) != 0;
        uint64_t next       = data_start + chunk_size + (has_pad ? 1 : 0);

        if (next > riff_end)
            break;

        if (id == fourCC("fmt ")) {
            if (!parseFmt(in, chunk_size, info.config, info.block_align))
                return false;
            found_fmt = true;
        }
        else if (id == fourCC("data")) {
            info.data_offset    = data_start;
            info.data_size      = chunk_size;
            // Skip past the RIFF padding byte (if data size is odd).
            // The padding byte is reconstructed during decompression.
            info.trailer_offset = data_start + chunk_size + (has_pad ? 1 : 0);
            found_data = true;
        }

        offset = next;
    }

    // Trailing data extends to the real end of file (may go beyond RIFF)
    if (found_data && info.trailer_offset < info.file_size) {
        // trailer_offset already set, file_size already set — good
    } else if (found_data) {
        info.trailer_offset = info.file_size; // no trailing data
    }

    return found_fmt && found_data;
}

} // namespace wav
} // namespace audiovcs
