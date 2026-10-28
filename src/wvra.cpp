#include "wvra.h"

#include "wavpack_utils.h"

#include <iostream>
#include <cstring>
#include <vector>

#include <zlib.h>

#define RIFF_CHUNK_START_OFFSET 12
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
    finfo.size = 0;
    finfo.type = FileType::Unknown;

    FourCC id;
    input.seekg(0);
    input.read(id.data(), 4);

    if (id != fourCC("RIFF"))
        return false;
    
    finfo.size = readU32LE(input, 4) + 8;
    input.read(id.data(), 4);

    if (id == fourCC("WAVE"))
        finfo.type = FileType::WAV;
    else if (id == fourCC("WVRA"))
        finfo.type = FileType::WVRA;
    else
        return false;
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
    bool success =  walkChunks(input, RIFF_CHUNK_START_OFFSET, finfo.size,
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
    if (dst.bad())
    {
        std::cout << "Could not write fmt header" << std::endl;
        return false;
    }

    size_t start = chunk.dataOffset; 
    size_t size = chunk.dataSize + (chunk.hasPadding ? 1 : 0);

    if (size < 16)
    {
        std::cout << "fmt size is <16, which should be the minimum" << std::endl;
        return false;
    }
        

    char buf[40] = {0};
    size_t toRead = size < sizeof(buf) ? size : sizeof(buf);
    if ( src.read(buf, toRead).bad() || src.gcount() < toRead)
    {
        std::cout << "Could not read bytes from src: " << toRead << std::endl; 
        return false;
    }
    
    dst.write(buf, toRead);
    if (dst.bad())
    {
        std::cout << "Could not write bytes in dst: " << toRead << std::endl;
        return false;
    }

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
    if (src.read(rbuf, remaining).bad() || src.gcount() < remaining)
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

bool s_processData(std::istream& src, std::ostream& dst, const ChunkInfo& chunk, const WaveHeader& fmt)
{
    if (fmt.NumChannels == 0 || fmt.SampleRate == 0 || fmt.BitsPerSample == 0)
        return false;

    dst.write("data", 4);
    writeU32LE(dst, 8); // placeholder crc32
    uint64_t datapos = dst.tellp();
    writeU32LE(dst, 0); // placeholder size
    writeU32LE(dst, 0); // placeholder crc32
    if (dst.bad()) return false;

    uint64_t startpos = dst.tellp();
    {
        WavpackEncoder encoder(dst);
        if (encoder.bad())
            return false;
        if (!encoder.configure(fmt, chunk.dataSize))
            return false;

        std::vector<char> raw(BUFFER_SIZE * fmt.BlockAlign);
        uint64_t remaining = chunk.dataSize;
        while (remaining > 0)
        {
            const uint64_t count = std::min<uint64_t>(raw.size(), remaining);
            if (count % fmt.BlockAlign != 0) {
                std::cout << "Bad align: " << count << " " << fmt.BlockAlign << " " << count % fmt.BlockAlign << std::endl; 
                return false;
            }
            if (src.read(raw.data(), count).bad() || src.gcount() < count) {
                std::cout << "Bad read" << std::endl;
                return false;
            }
            if (!encoder.encode(raw.data(), count)) {
                std::cout << "Bad encode" << std::endl;
                return false;
            }
            remaining -= count;
        }

        if (!encoder.flush()) {
            std::cout << "Bad flush" << std::endl;
            return false;
        }
    }

    uint64_t endpos = dst.tellp();
    uint64_t compressedSize = endpos - startpos;
    dst.seekp(datapos);
    writeU32LE(dst, compressedSize);
    dst.seekp(endpos);
    if (compressedSize & 1)
        dst.put(0);

    return dst.good();
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
        if (src.read(bytes, count).bad() || src.gcount() < count)
            return false;
        if (dst.write(bytes, count).bad())
            return false;
        remaining -= count;
    }
    return true;
}

bool wvra::compress(const std::string& srcPath, const std::string& dstPath)
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

    std::ofstream dst(dstPath, std::ios::binary);
    // RIFF header
    dst.write("RIFF", 4);
    writeU32LE(dst, 0);
    dst.write("WVRA", 4);

    // wave old size chunk
    dst.write("wave", 4);
    writeU32LE(dst, 4);
    writeU32LE(dst, finfo.size);
    

    WaveHeader fmt{};
    std::memset(&fmt, 0, sizeof(fmt));

    std::cout << "=== CHUNKS ===" << std::endl;
    bool success =  walkChunks(src, RIFF_CHUNK_START_OFFSET, finfo.size,
        [&](const ChunkInfo& chunk, std::istream& s) -> bool {
            std::cout << "CHUNK: "  << std::string_view(chunk.id.data(), 4) << " " << chunk.dataSize << std::endl;
            if (chunk.id == fourCC("fmt "))
                return s_processFmt(s, dst, chunk, fmt);
            else if (chunk.id == fourCC("data"))
                return s_processData(s, dst, chunk, fmt);
            return s_copyChunk(s, dst, chunk);
        }
    );

    if (!success)
        return false;

    uint32_t riffsize = static_cast<uint32_t>(dst.tellp()) - 8;
    dst.seekp(4);
    writeU32LE(dst, riffsize); // Write the new size of the full file
    return dst.good();
}

bool wvra::uncompress(const std::string& srcPath, const std::string& dstPath)
{
    return true;
}