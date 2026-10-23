#include "wvra.h"

#include <iostream>
#include <cstring>

#include <zlib.h>

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
        [&](const ChunkInfo& chunk, std::istream& source) -> bool {
            std::cout << std::string_view(chunk.id.data(), 4) << std::endl;
            std::cout << "  SIZE: " << chunk.dataSize << std::endl; 
            source.seekg(chunk.dataOffset);
            uint64_t remaining = chunk.dataSize;
            uLong crc = crc32(0UL, Z_NULL, 0);
            uint8_t bytes[4096];
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
    uint8_t bytes[4096];

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