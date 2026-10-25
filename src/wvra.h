#pragma once

#include <array>
#include <cstdint>
#include <fstream>

namespace wvra {

using FourCC = std::array<char, 4>;

constexpr FourCC fourCC(const char (&s)[5]) noexcept {
    return {s[0], s[1], s[2], s[3]};
}

struct ChunkInfo {
    FourCC id;

    uint64_t headerOffset;
    uint64_t dataOffset;
    uint64_t dataSize;

    bool hasPadding;
};

enum FileType {Unknown, WAV, WVRA};

struct FileInfo {
    FileType type;
    size_t size;
    uint64_t chunkStartOffset;
};

bool readHeader(std::istream& in, FileInfo& finfo);

void printFileInfo(const FileInfo& finfo);

template<typename Callback>
bool walkChunks(std::istream& in, uint64_t startOffset, uint64_t endOffset, Callback&& callback);

bool analyse(const std::string& path);

bool compress(const std::string& srcPath, const std::string& dstPath);
bool uncompress(const std::string& srcPath, const std::string& dstPath);

};