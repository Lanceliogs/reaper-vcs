#include "wvra.h"

#include <wavpack.h>

#include <iostream>
#include <cstring>
#include <vector>

#include <zlib.h>

#define BUFFER_SIZE 4096

using namespace wvra;

/* Little endian helpers */

static uint16_t u16LE(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) + (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t u32LE(const uint8_t* p, size_t count)
{
    uint32_t value = 0;
    for (size_t i = 0; i < count && i < 4; ++i)
        value += static_cast<uint32_t>(p[i]) << (8 * i);
    return value;
}

static uint32_t readU32LE(std::istream& input, size_t count)
{
    uint8_t buf[count];
    input.read((char*)buf, count);
    return u32LE(buf, count);
}

static void writeU32LE(std::ostream& dst, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        dst.put(static_cast<char>((value >> (i * 8)) & 0xFF));
}

bool wvra::readHeader(std::istream& input, FileInfo& finfo)
{
    FourCC id;
    input.seekg(0);
    input.read(id.data(), 4);

    if (id == fourCC("RIFF"))
    {
        finfo.type = FileType::WAV;
        finfo.chunkStartOffset = 12;
    }
    else if (id == fourCC("WVRA"))
    {
        finfo.type = FileType::WVRA;
        finfo.chunkStartOffset = 20;
    }
    else
    {
        finfo.type = FileType::Unknown;
        finfo.size = 0;
        finfo.chunkStartOffset = 0;
        return false;
    }
    
    finfo.size = readU32LE(input, 4) + 8;

    return true;
}

void wvra::printFileInfo(const FileInfo& finfo)
{
    switch (finfo.type) {
        case FileType::WAV:
            std::cout << "TYPE: WAV" << std::endl;
            break;
        case FileType::WVRA:
            std::cout << "TYPE: WVRA" << std::endl;
            break;
    }
    std::cout << "SIZE: " << finfo.size << std::endl;
    std::cout << "HEADER SIZE: " << finfo.chunkStartOffset << std::endl;
}

template<typename Callback>
bool wvra::walkChunks(std::istream& input, uint64_t startOffset, uint64_t endOffset, Callback&& callback)
{
    uint64_t offset = startOffset;

    while (offset < endOffset)
    {
        input.seekg(offset);

        ChunkInfo chunk{};
        input.read(chunk.id.data(), 4);

        if (!input)
            return false;

        uint32_t size = readU32LE(input, 4);

        chunk.headerOffset = offset;
        chunk.dataOffset   = offset + 8;
        chunk.dataSize     = size;
        chunk.hasPadding   = (size & 1) != 0;

        uint64_t nextOffset =
            chunk.dataOffset +
            chunk.dataSize +
            (chunk.hasPadding ? 1 : 0);

        if (nextOffset > endOffset)
            return false;

        input.seekg(chunk.dataOffset);

        if (!callback(chunk, input))
            return false;

        // Callback may have read or sought anywhere.
        // The walker always restores the correct position.
        offset = nextOffset;
    }

    return offset == endOffset;
}

bool wvra::analyse(const std::string& path)
{
    std::cout << "Analysing " << path << std::endl;
    std::ifstream input(path, std::ios::binary);
    FileInfo finfo{};
    if (!readHeader(input, finfo))
        return false;
    
    std::cout << "=== HEADER ===" << std::endl; 
    printFileInfo(finfo);

    std::cout << "=== CHUNKS ===" << std::endl;
    bool success =  walkChunks(input, finfo.chunkStartOffset, finfo.size,
        [](const ChunkInfo& chunk, std::istream& source) -> bool {
            std::cout << std::string_view(chunk.id.data(), 4) << std::endl;
            std::cout << "  SIZE: " << chunk.dataSize << std::endl; 
            uint64_t remaining = chunk.dataSize;
            uLong crc = crc32(0UL, Z_NULL, 0);
            uint8_t bytes[BUFFER_SIZE];
            while (remaining > 0)
            {
                uint64_t toRead = std::min(remaining, sizeof(bytes));
                source.read((char*)bytes, toRead);
                crc = crc32(crc, bytes, toRead);
                remaining -= toRead;
            }
            std::cout << "  CRC: " << crc << std::endl;
            return true;
        }
    );

    if (!success)
        return false;

    // Seek to the end of the declared RIFF container.
    input.clear();
    input.seekg(finfo.size);

    if (!input)
        return false;

    uint64_t trailingSize = 0;
    uLong crc = crc32(0L, Z_NULL, 0);
    uint8_t bytes[BUFFER_SIZE];

    while (true)
    {
        input.read((char*)bytes, sizeof(bytes));
        const std::streamsize count = input.gcount();
        if (count > 0)
        {
            trailingSize += static_cast<uint64_t>(count);
            crc = crc32(crc, bytes, count);
        }

        if (input.bad())    return false;
        if (input.eof())    break;
        if (!input)         return false;
    }

    if (trailingSize > 0)
    {
        std::cout << "=== TRAILING DATA ===" << std::endl;
        std::cout << "  SIZE: " << trailingSize << '\n';
        std::cout << "  CRC: " << static_cast<uint32_t>(crc) << '\n';
    }

    return true;
}

static bool s_processFmt(std::istream &src, std::ostream &dst, const ChunkInfo& chunk, WaveHeader& fmt)
{
    dst.write(chunk.id.data(), 4);
    writeU32LE(dst, chunk.dataSize);
    if (dst.bad()) return false;

    size_t start = chunk.dataOffset; 
    size_t size = chunk.dataSize + (chunk.hasPadding ? 1 : 0);

    if (size < 16)
        return false;

    char buf[40] = {0};
    size_t toRead = size < sizeof(buf) ? size : sizeof(buf);

    if (src.read(buf, toRead).bad() ||src.gcount() != toRead);
        return false;
    if (dst.write(buf, toRead).bad())
        return false;

    // The first 16 bytes are present in every standard WAVE fmt chunk.
    std::memcpy(&fmt.FormatTag,       buf + 0,  2);
    std::memcpy(&fmt.NumChannels,     buf + 2,  2);
    std::memcpy(&fmt.SampleRate,      buf + 4,  4);
    std::memcpy(&fmt.BytesPerSecond,  buf + 8,  4);
    std::memcpy(&fmt.BlockAlign,      buf + 12, 2);
    std::memcpy(&fmt.BitsPerSample,   buf + 14, 2);

    if (size >= 18) std::memcpy(&fmt.cbSize, buf + 16, 2);
    if (size >= 20) std::memcpy(&fmt.ValidBitsPerSample, buf + 18, 2);
    if (size >= 24) std::memcpy(&fmt.ChannelMask, buf + 20, 4);
    if (size >= 26) std::memcpy(&fmt.SubFormat, buf + 24, 2);
    if (size >= 40) std::memcpy(fmt.GUID, buf + 26, 14);

    // Skip any fmt payload bytes we didn't read, plus RIFF padding.
    long remaining = static_cast<long>(size - toRead);
    if (remaining == 0)
        return true;
    
    char rbuf[remaining];
    if (src.read(rbuf, remaining).bad() ||src.gcount() < remaining)
        return false;

    return dst.write(rbuf, sizeof(rbuf)).good();
}

static int s_writeBlock(void* id, void* data, int32_t count)
{
    std::ostream *dst = static_cast<std::ostream*>(id);
    if (count < 0) return 0;
    dst->write((char*)data, count);
    return dst->good() ? 1 : 0;
}

int32_t s_pcmSample(const uint8_t* p, size_t bytes)
{
    uint32_t value = u32LE(p, bytes);
    const unsigned bits = static_cast<unsigned>(bytes * 8);

    if (bits < 32 && (value & (uint32_t{1} << (bits - 1))))
        value |= (~uint32_t{0} << bits);

    return static_cast<int32_t>(value);
}

bool s_processData(std::istream& src, std::ostream& dst, const ChunkInfo& chunk, const WaveHeader& fmt)
{
    if (fmt.NumChannels == 0 || fmt.SampleRate == 0 || fmt.BitsPerSample == 0)
        return false;
    
        WavpackContext* ctx = WavpackOpenFileOutput(s_writeBlock, &dst, nullptr);
    if (!ctx)
        return false;

    WavpackConfig cfg{};

    cfg.num_channels = fmt.NumChannels;
    cfg.sample_rate = fmt.SampleRate;
    cfg.bits_per_sample = fmt.BitsPerSample;
    cfg.bytes_per_sample = (fmt.BitsPerSample + 7) / 8;
    cfg.channel_mask = fmt.ChannelMask;

    // TODO: Set the correct channel mask and lossless flags.

    int64_t total_samples = chunk.dataSize / fmt.BlockAlign;
    bool ok = WavpackSetConfiguration64(ctx, &cfg, total_samples, nullptr) && WavpackPackInit(ctx);

    const size_t stride = fmt.BlockAlign / fmt.NumChannels;
    std::vector<uint8_t> raw(fmt.BlockAlign * BUFFER_SIZE);
    std::vector<int32_t> samples(fmt.NumChannels * BUFFER_SIZE);

    uint64_t remaining = chunk.dataSize;
    while (ok && remaining > 0)
    {
        const uint64_t count = std::min<uint64_t>(raw.size(), remaining);
        if (count % fmt.BlockAlign != 0)
        {
            ok = false;
            break;
        }
        if (src.read((char*)raw.data(), count).bad() || src.gcount() != count)
        {
            ok = false;
            break;
        }

        const size_t frames = count / fmt.BlockAlign;
        const size_t sampleCount = frames * fmt.NumChannels;

        for (size_t i = 0; i < sampleCount; ++i)
            samples[i] = s_pcmSample(raw.data() + i * stride, stride);

        if (!WavpackPackSamples(ctx, samples.data(), frames))
            ok = false;

        remaining -= count;
    }

    if (ok) ok = WavpackFlushSamples(ctx);
    WavpackCloseFile(ctx);
    
    return ok;
}

bool s_copyChunk(std::istream& src, std::ostream& dst, const ChunkInfo& chunk)
{
    dst.write(chunk.id.data(), 4);
    writeU32LE(dst, chunk.dataSize);
    if (dst.bad()) return false;

    char bytes[BUFFER_SIZE];
    uint32_t remaining = chunk.dataSize + (chunk.hasPadding ? 1 : 0);
    while (remaining > 0)
    {
        uint32_t count = std::min(remaining, static_cast<uint32_t>(BUFFER_SIZE));
        if (src.read(bytes, count).bad() || src.gcount() != count)
            return false;
        if (dst.write(bytes, count).bad())
            return false;
    }
    return true;
}

bool compress(const std::string& srcPath, const std::string& dstPath)
{
    std::ifstream src(srcPath, std::ios::binary);

    FileInfo finfo{};
    if (!readHeader(src, finfo))
    {
        std::cerr << "Unrecognized format: Bad header" << std::endl;
        return false;
    }

    printFileInfo(finfo);
    if (finfo.type != FileType::WAV)
    {
        std::cerr << "Invalid format: Not WAV" << std::endl;
        return false;
    }

    std::ofstream dst(srcPath, std::ios::binary);
    dst.write("WVRA", 4);
    writeU32LE(dst, 0);
    dst.write("RIFF", 4);
    writeU32LE(dst, finfo.size);
    dst.write("WAVE", 4);

    WaveHeader fmt{};
    std::memset(&fmt, 0, sizeof(fmt));

    std::cout << "=== CHUNKS ===" << std::endl;
    bool success =  walkChunks(src, finfo.chunkStartOffset, finfo.size,
        [&](const ChunkInfo& chunk, std::istream& source) -> bool {
            if (chunk.id == fourCC("fmt "))
                return s_processFmt(src, dst, chunk, fmt);
            else if (chunk.id == fourCC("data"))
                return s_processData(src, dst, chunk, fmt);
            return s_copyChunk(src, dst, chunk);
        }
    );

    uint32_t riffsize = static_cast<uint32_t>(dst.tellp()) - 8;
    dst.seekp(4);
    writeU32LE(dst, riffsize); // Write the new size of the full file
    return dst.good();
}

bool uncompress(const std::string& srcPath, const std::string& dstPath)
{
    return true;
}