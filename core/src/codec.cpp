#include "audiovcs/codec.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#define BUFFER_SAMPLES 4096

namespace audiovcs {
namespace codec {

// ─── helpers ───────────────────────────────────────────────────────────

static int s_writeBlock(void* id, void* data, int32_t count)
{
    auto* out = static_cast<std::ostream*>(id);
    if (count <= 0) return 0;
    out->write(static_cast<char*>(data), count);
    return out->good() ? 1 : 0;
}

static int32_t s_pcmToInt32(const uint8_t* p, int bytes)
{
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i)
        v |= uint32_t(p[i]) << (i * 8);

    // Sign-extend for integer formats (not needed for 32-bit)
    if (bytes < 4 && (v & (uint32_t{1} << (bytes * 8 - 1))))
        v |= ~uint32_t{0} << (bytes * 8);

    return static_cast<int32_t>(v);
}

static void s_int32ToPcm(uint8_t* p, int32_t sample, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        p[i] = static_cast<uint8_t>((sample >> (i * 8)) & 0xFF);
}

// ─── compress ──────────────────────────────────────────────────────────

bool compress(const std::string& wav_path, const wav::SplitInfo& info,
              const std::string& wv_path)
{
    std::ifstream src(wav_path, std::ios::binary);
    if (!src) {
        std::cerr << "codec: cannot open " << wav_path << std::endl;
        return false;
    }

    std::ofstream dst(wv_path, std::ios::binary);
    if (!dst) {
        std::cerr << "codec: cannot create " << wv_path << std::endl;
        return false;
    }

    // Read header wrapper [0, data_offset)
    std::vector<char> header(info.data_offset);
    src.seekg(0);
    src.read(header.data(), header.size());
    if (!src) return false;

    // Open WavPack encoder
    WavpackContext* ctx = WavpackOpenFileOutput(s_writeBlock, &dst, nullptr);
    if (!ctx) return false;

    WavpackSetFileInformation(ctx, const_cast<char*>("wav"), WP_FORMAT_WAV);

    // Attach the header wrapper (RIFF header + fmt + data chunk header)
    if (!WavpackAddWrapper(ctx, header.data(), static_cast<uint32_t>(header.size()))) {
        std::cerr << "codec: WavpackAddWrapper (header) failed" << std::endl;
        WavpackCloseFile(ctx);
        return false;
    }

    // Configure encoding
    WavpackConfig cfg = info.config;
    int64_t total_samples = info.data_size / info.block_align;

    if (!WavpackSetConfiguration64(ctx, &cfg, total_samples, nullptr) ||
        !WavpackPackInit(ctx)) {
        std::cerr << "codec: WavPack config/init failed: "
                  << WavpackGetErrorMessage(ctx) << std::endl;
        WavpackCloseFile(ctx);
        return false;
    }

    // Pack samples
    const int bps = cfg.bytes_per_sample;
    const int nch = cfg.num_channels;
    const int frame_bytes = info.block_align;
    std::vector<char>    raw(BUFFER_SAMPLES * frame_bytes);
    std::vector<int32_t> buf(BUFFER_SAMPLES * nch);

    src.seekg(info.data_offset);
    uint64_t remaining = info.data_size;

    while (remaining > 0) {
        uint64_t to_read = std::min<uint64_t>(raw.size(), remaining);
        // Align to whole frames
        to_read -= to_read % frame_bytes;
        if (to_read == 0) break;

        src.read(raw.data(), to_read);
        if (src.gcount() < static_cast<std::streamsize>(to_read)) {
            std::cerr << "codec: short read from WAV" << std::endl;
            WavpackCloseFile(ctx);
            return false;
        }

        size_t sample_count = to_read / frame_bytes;
        size_t total_channels = sample_count * nch;

        for (size_t i = 0; i < total_channels; ++i)
            buf[i] = s_pcmToInt32(
                reinterpret_cast<uint8_t*>(raw.data()) + i * bps, bps);

        if (!WavpackPackSamples(ctx, buf.data(), sample_count)) {
            std::cerr << "codec: WavpackPackSamples failed: "
                      << WavpackGetErrorMessage(ctx) << std::endl;
            WavpackCloseFile(ctx);
            return false;
        }

        remaining -= to_read;
    }

    // Read trailing wrapper [trailer_offset, file_size)
    // This includes the RIFF padding byte (if any) and any post-data chunks.
    uint64_t trailer_size = info.file_size - info.trailer_offset;
    if (trailer_size > 0) {
        std::vector<char> trailer(trailer_size);
        src.seekg(info.trailer_offset);
        src.read(trailer.data(), trailer.size());
        if (!src) {
            std::cerr << "codec: cannot read trailing data" << std::endl;
            WavpackCloseFile(ctx);
            return false;
        }

        if (!WavpackAddWrapper(ctx, trailer.data(),
                               static_cast<uint32_t>(trailer_size))) {
            std::cerr << "codec: WavpackAddWrapper (trailer) failed" << std::endl;
            WavpackCloseFile(ctx);
            return false;
        }
    }

    // Flush writes all pending data including the trailing wrapper.
    // Must be called AFTER WavpackAddWrapper for the trailer.
    if (!WavpackFlushSamples(ctx)) {
        std::cerr << "codec: WavpackFlushSamples failed: "
                  << WavpackGetErrorMessage(ctx) << std::endl;
        WavpackCloseFile(ctx);
        return false;
    }

    WavpackCloseFile(ctx);
    return dst.good();
}

// ─── decompress ────────────────────────────────────────────────────────

bool decompress(const std::string& wv_path, const std::string& wav_path)
{
    char error[80] = {0};
    WavpackContext* ctx = WavpackOpenFileInput(
        wv_path.c_str(), error, OPEN_WRAPPER | OPEN_FILE_UTF8, 0);

    if (!ctx) {
        std::cerr << "codec: cannot open " << wv_path << ": " << error << std::endl;
        return false;
    }

    std::ofstream dst(wav_path, std::ios::binary);
    if (!dst) {
        std::cerr << "codec: cannot create " << wav_path << std::endl;
        WavpackCloseFile(ctx);
        return false;
    }

    // Write header wrapper (RIFF header + fmt + data chunk header)
    uint32_t wrapper_bytes = WavpackGetWrapperBytes(ctx);
    if (wrapper_bytes > 0) {
        unsigned char* wrapper_data = WavpackGetWrapperData(ctx);
        dst.write(reinterpret_cast<char*>(wrapper_data), wrapper_bytes);
        WavpackFreeWrapper(ctx);
    }

    // Decode audio samples
    const int nch = WavpackGetNumChannels(ctx);
    const int bps = WavpackGetBytesPerSample(ctx);
    int64_t total = WavpackGetNumSamples64(ctx);

    std::vector<int32_t> buf(BUFFER_SAMPLES * nch);
    std::vector<uint8_t> pcm(BUFFER_SAMPLES * nch * bps);

    int64_t decoded = 0;
    while (decoded < total) {
        uint32_t to_unpack = static_cast<uint32_t>(
            std::min<int64_t>(BUFFER_SAMPLES, total - decoded));

        uint32_t got = WavpackUnpackSamples(ctx, buf.data(), to_unpack);
        if (got == 0) break;

        size_t channels_total = got * nch;
        for (size_t i = 0; i < channels_total; ++i)
            s_int32ToPcm(pcm.data() + i * bps, buf[i], bps);

        dst.write(reinterpret_cast<char*>(pcm.data()), channels_total * bps);
        decoded += got;
    }

    if (decoded != total) {
        std::cerr << "codec: decoded " << decoded << " of " << total
                  << " samples" << std::endl;
        WavpackCloseFile(ctx);
        return false;
    }

    // RIFF requires chunk data to be word-aligned. If the PCM data has an
    // odd byte count, write the padding byte that the encoder skipped.
    uint64_t pcm_bytes = static_cast<uint64_t>(decoded) * nch * bps;
    if (pcm_bytes & 1)
        dst.put(0);

    // Write trailing wrapper (post-data chunks, trailing junk)
    WavpackSeekTrailingWrapper(ctx);
    wrapper_bytes = WavpackGetWrapperBytes(ctx);
    if (wrapper_bytes > 0) {
        unsigned char* wrapper_data = WavpackGetWrapperData(ctx);
        dst.write(reinterpret_cast<char*>(wrapper_data), wrapper_bytes);
        WavpackFreeWrapper(ctx);
    }

    WavpackCloseFile(ctx);
    return dst.good();
}

} // namespace codec
} // namespace audiovcs
