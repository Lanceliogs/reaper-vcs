
#include <wavpack.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstring>

#define CHUNK_SIZE (2048 * 3) 

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

static uint32_t u32(const unsigned char* p, size_t count)
{
    uint32_t value = 0;
    for (size_t i = 0; i < count && i < 4; ++i)
        value |= static_cast<uint32_t>(p[i]) << (8 * i);
    return value;
}

int32_t pcmSample(const unsigned char* p, size_t bytes)
{
    uint32_t value = u32(p, bytes);
    const unsigned bits = static_cast<unsigned>(bytes * 8);

    if (bits < 32 && (value & (uint32_t{1} << (bits - 1))))
        value |= (~uint32_t{0} << bits);

    return static_cast<int32_t>(value);
}

bool parseFmt(FILE* file, uint32_t chunkSize, WaveHeader& fmt)
{
    if (chunkSize < 16)
        return false;

    unsigned char buf[40]{};
    size_t toRead = chunkSize < sizeof(buf) ? chunkSize : sizeof(buf);

    if (std::fread(buf, 1, toRead, file) != toRead)
        return false;

    // The first 16 bytes are present in every standard WAVE fmt chunk.
    std::memcpy(&fmt.FormatTag,       buf + 0,  2);
    std::memcpy(&fmt.NumChannels,     buf + 2,  2);
    std::memcpy(&fmt.SampleRate,      buf + 4,  4);
    std::memcpy(&fmt.BytesPerSecond,  buf + 8,  4);
    std::memcpy(&fmt.BlockAlign,      buf + 12, 2);
    std::memcpy(&fmt.BitsPerSample,   buf + 14, 2);

    if (chunkSize >= 18)
        std::memcpy(&fmt.cbSize, buf + 16, 2);

    if (chunkSize >= 20)
        std::memcpy(&fmt.ValidBitsPerSample, buf + 18, 2);

    if (chunkSize >= 24)
        std::memcpy(&fmt.ChannelMask, buf + 20, 4);

    if (chunkSize >= 26)
        std::memcpy(&fmt.SubFormat, buf + 24, 2);

    if (chunkSize >= 40)
        std::memcpy(fmt.GUID, buf + 26, 14);

    // Skip any fmt payload bytes we didn't read, plus RIFF padding.
    long remaining = static_cast<long>(chunkSize - toRead + (chunkSize & 1));
    return remaining == 0 || std::fseek(file, remaining, SEEK_CUR) == 0;
}

void printFmt(const WaveHeader& fmt)
{
    std::cout << "FormatTag: " << fmt.FormatTag << std::endl;
    std::cout << "NumChannels: " << fmt.NumChannels << std::endl;
    std::cout << "SampleRate: " << fmt.SampleRate << std::endl;
    std::cout << "BytesPerSecond: " << fmt.BytesPerSecond << std::endl;
    std::cout << "BlockAlign: " << fmt.BlockAlign << std::endl;
    std::cout << "BitsPerSample: " << fmt.BitsPerSample << std::endl;
    std::cout << "cbSize: " << fmt.cbSize << std::endl;
    std::cout << "ValidBitsPerSample: " << fmt.ValidBitsPerSample << std::endl;
    std::cout << "ChannelMask: " << fmt.ChannelMask << std::endl;
    std::cout << "SubFormat: " << fmt.SubFormat << std::endl;
    std::cout << "GUID: " << fmt.GUID << std::endl;
}

// Supports canonical 44-byte, 16-bit PCM WAV files.
bool compressWav(const char* wavPath, const char* wvPath)
{
    File in(std::fopen(wavPath, "rb"), &std::fclose);
    File out(std::fopen(wvPath, "wb"), &std::fclose);
    if (!in || !out) return false;

    WaveHeader fmt{};
    uint32_t dataBytes = 0;
    bool foundFmt = false;

    RiffChunkHeader riff;
    if (std::fread(&riff, 1, sizeof(riff), in.get()) != sizeof(riff))
        return false;

    if (std::memcmp(riff.ckID, "RIFF", 4) != 0 ||
        std::memcmp(riff.formType, "WAVE", 4) != 0)
        return false;

    while (true) {
        ChunkHeader chunk{};

        if (std::fread(&chunk, 1, sizeof(chunk), in.get()) != sizeof(chunk))
            return false;

        if (std::memcmp(chunk.ckID, "fmt ", 4) == 0) {
            if (!parseFmt(in.get(), chunk.ckSize, fmt))
                return false;
            printFmt(fmt);
            foundFmt = true;
        }
        else if (std::memcmp(chunk.ckID, "data", 4) == 0) {
            dataBytes = chunk.ckSize;
            std::cout << "data chunk found: " << dataBytes << std::endl;
            break; // FILE* points at audio data.
        }
        else {
            long skip = static_cast<long>(chunk.ckSize + (chunk.ckSize & 1));
            if (std::fseek(in.get(), skip, SEEK_CUR) != 0)
                return false;
        }
    }

    if (!foundFmt)
        return false; 

    WavpackContext* ctx =
        WavpackOpenFileOutput(writeBlock, out.get(), nullptr);
    if (!ctx) return false;

    WavpackConfig cfg{};
    cfg.bits_per_sample = fmt.BitsPerSample;
    cfg.bytes_per_sample = fmt.BlockAlign;
    cfg.num_channels = fmt.NumChannels;
    cfg.channel_mask = fmt.ChannelMask;
    cfg.sample_rate = fmt.SampleRate;

    if (fmt.FormatTag != 1 || fmt.NumChannels == 0 || fmt.BitsPerSample == 0 || fmt.BitsPerSample > 32)
        return false;

    int64_t total_samples = dataBytes / fmt.BlockAlign;
    bool ok = WavpackSetConfiguration64(ctx, &cfg, total_samples, nullptr)
        && WavpackPackInit(ctx);

    const size_t stride = fmt.BlockAlign / fmt.NumChannels;
    std::vector<unsigned char> raw(fmt.BlockAlign * 2048);
    std::vector<int32_t> samples(2048 * fmt.NumChannels);

    uint64_t remaining = dataBytes;
    while (ok && remaining > 0)
    {
        const size_t chunkSize = static_cast<size_t>(std::min<uint64_t>(raw.size(), remaining));
        const size_t n = std::fread(raw.data(), 1, chunkSize, in.get());

        if (n != chunkSize) {
            ok = false;
            break;
        }

        if (n % fmt.BlockAlign != 0) {
            ok = false;
            break;
        }

        const size_t frames = n / fmt.BlockAlign;
        const size_t sampleCount = frames * fmt.NumChannels;

        for (size_t i = 0; i < sampleCount; ++i)
            samples[i] = pcmSample(raw.data() + i * stride, stride);

        if (!WavpackPackSamples(ctx, samples.data(), frames))
            ok = false;

        remaining -= n;
    }

    if (ok) ok = WavpackFlushSamples(ctx);
    WavpackCloseFile(ctx);
    return ok && !std::ferror(out.get());
}

bool decompressWv(const char* wvPath, const char* wavPath)
{
    char error[128] = {0};
    WavpackContext* ctx = WavpackOpenFileInput(wvPath, error, OPEN_WRAPPER, 0);

    if (!ctx)
    {
        std::cerr << error << '\n';
        return false;
    }

    File out(std::fopen(wavPath, "wb"), &std::fclose);
    if (!out)
    {
        WavpackCloseFile(ctx);
        return false;
    }

    const int channels = WavpackGetNumChannels(ctx);
    const int bits = WavpackGetBitsPerSample(ctx);
    const int bytes = WavpackGetBytesPerSample(ctx);

    uint32_t wrapperBytes = WavpackGetWrapperBytes(ctx);
    unsigned char* wrapper = WavpackGetWrapperData(ctx);

    bool ok = channels >= 1 &&
              bits >= 1 && bits <= 32 &&
              bytes == (bits + 7) / 8 &&
              wrapper != nullptr && wrapperBytes != 0 &&
              std::fwrite(wrapper, 1, wrapperBytes, out.get())
                  == wrapperBytes;

    constexpr uint32_t batchFrames = 8192;
    std::vector<int32_t> samples(
        static_cast<size_t>(batchFrames) * channels);
    std::vector<unsigned char> raw(
        static_cast<size_t>(batchFrames) * channels * bytes);

    while (ok) {
        const uint32_t frames =
            WavpackUnpackSamples(ctx, samples.data(), batchFrames);

        if (frames == 0)
            break;

        const size_t sampleCount = static_cast<size_t>(frames) * channels;

        for (size_t i = 0; i < sampleCount; ++i) {
            const uint32_t value = static_cast<uint32_t>(samples[i]);

            for (int b = 0; b < bytes; ++b)
                raw[i * bytes + b] =
                    static_cast<unsigned char>(value >> (8 * b));
        }

        const size_t bytesToWrite = sampleCount * bytes;

        if (std::fwrite(raw.data(), 1, bytesToWrite, out.get())
            != bytesToWrite)
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
