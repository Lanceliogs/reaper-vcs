
#include <wavpack.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using File = std::unique_ptr<FILE, decltype(&std::fclose)>;

static int writeBlock(void* id, void* data, int32_t size)
{
    return std::fwrite(data, 1, size, static_cast<FILE*>(id))
           == static_cast<size_t>(size);
}

static uint16_t u16(const unsigned char* p)
{
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

static uint32_t u32(const unsigned char* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Supports canonical 44-byte, 16-bit PCM WAV files.
bool compressWav(const char* wavPath, const char* wvPath)
{
    File in(std::fopen(wavPath, "rb"), &std::fclose);
    File out(std::fopen(wvPath, "wb"), &std::fclose);
    if (!in || !out) return false;

    unsigned char header[44];
    if (std::fread(header, 1, sizeof(header), in.get()) != sizeof(header))
        return false;

    if (std::memcmp(header, "RIFF", 4) ||
        std::memcmp(header + 8, "WAVE", 4) ||
        std::memcmp(header + 12, "fmt ", 4) ||
        std::memcmp(header + 36, "data", 4) ||
        u16(header + 20) != 1 || u16(header + 34) != 16)
        return false;

    const int bits_per_sample  = u16(header + 34);
    const int bytes_per_sample = (bits_per_sample + 7) / 8; // rounding up one byte
    const int channels = u16(header + 22);
    const int rate = static_cast<int>(u32(header + 24));
    const uint32_t dataBytes = u32(header + 40);

    if ((channels != 1 && channels != 2) || rate <= 0 ||
        dataBytes % (channels * 2) != 0)
        return false;

    const int64_t frames = dataBytes / (channels * 2);

    WavpackContext* ctx =
        WavpackOpenFileOutput(writeBlock, out.get(), nullptr);
    if (!ctx) return false;

    WavpackConfig cfg{};
    cfg.bits_per_sample = bits_per_sample;
    cfg.bytes_per_sample = bytes_per_sample;
    cfg.num_channels = channels;
    cfg.channel_mask = channels == 1 ? 4 : 3;
    cfg.sample_rate = rate;

    bool ok = WavpackSetConfiguration64(ctx, &cfg, frames, nullptr)
           && WavpackPackInit(ctx);

    std::vector<unsigned char> raw(8192);
    std::vector<int32_t> samples(8192 / 2);

    while (ok) {
        const size_t n = std::fread(raw.data(), 1, raw.size(), in.get());
        if (n == 0) {
            if (std::ferror(in.get())) ok = false;
            break;
        }
        if (n % (channels * 2) != 0) {
            ok = false;
            break;
        }

        const size_t count = n / 2;
        for (size_t i = 0; i < count; ++i) {
            const uint16_t bits = u16(raw.data() + i * 2);
            samples[i] = static_cast<int16_t>(bits);
        }

        if (!WavpackPackSamples(ctx, samples.data(),
                                static_cast<uint32_t>(count / channels)))
            ok = false;
    }

    if (ok) ok = WavpackFlushSamples(ctx);
    WavpackCloseFile(ctx);
    return ok && !std::ferror(out.get());
}

bool decompressWv(const char* wvPath, const char* wavPath)
{
    char error[128]{};
    WavpackContext* ctx =
        WavpackOpenFileInput(wvPath, error, OPEN_WRAPPER, 0);

    if (!ctx) {
        std::cerr << error << '\n';
        return false;
    }

    File out(std::fopen(wavPath, "wb"), &std::fclose);
    if (!out) {
        WavpackCloseFile(ctx);
        return false;
    }

    const int channels = WavpackGetNumChannels(ctx);
    const int bits = WavpackGetBitsPerSample(ctx);
    const int bytes = WavpackGetBytesPerSample(ctx);

    uint32_t wrapperBytes = WavpackGetWrapperBytes(ctx);
    unsigned char* wrapper = WavpackGetWrapperData(ctx);

    bool ok = channels >= 1 && channels <= 2 &&
              bits == 16 && bytes == 2 &&
              wrapper != nullptr && wrapperBytes != 0 &&
              std::fwrite(wrapper, 1, wrapperBytes, out.get())
                  == wrapperBytes;

    std::vector<int32_t> samples(8192 * channels);
    std::vector<unsigned char> raw(8192 * channels * 2);

    while (ok) {
        const uint32_t n =
            WavpackUnpackSamples(ctx, samples.data(), 8192);

        if (n == 0) break;

        for (size_t i = 0; i < size_t(n) * channels; ++i) {
            const uint16_t bits16 =
                static_cast<uint16_t>(samples[i]);
            raw[i * 2]     = bits16 & 0xff;
            raw[i * 2 + 1] = bits16 >> 8;
        }

        if (std::fwrite(raw.data(), 2, size_t(n) * channels,
                        out.get()) != size_t(n) * channels)
            ok = false;
    }

    WavpackCloseFile(ctx);
    return ok && !std::ferror(out.get());
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "Usage:\n"
                  << "  audio_tool encode input.wav output.wv\n"
                  << "  audio_tool decode input.wv output.wav\n";
        return 2;
    }

    const std::string mode = argv[1];
    const bool ok = mode == "encode"
        ? compressWav(argv[2], argv[3])
        : mode == "decode"
            ? decompressWv(argv[2], argv[3])
            : false;

    if (!ok) {
        std::cerr << "Conversion failed\n";
        return 1;
    }

    return 0;
}
