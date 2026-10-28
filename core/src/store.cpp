#include "audiovcs/store.h"
#include "audiovcs/codec.h"
#include "audiovcs/hash.h"
#include "audiovcs/wav.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace audiovcs {
namespace store {

std::string blob_path(const std::string& root, const std::string& hash,
                      const std::string& ext)
{
    if (hash.size() < 4) return "";
    // e.g. .audiovcs/objects/ab/cdef01234...wv
    return (fs::path(root) / "objects" / hash.substr(0, 2)
            / (hash.substr(2) + "." + ext)).string();
}

static std::string find_blob(const std::string& root, const std::string& hash)
{
    if (hash.size() < 4) return "";
    fs::path dir = fs::path(root) / "objects" / hash.substr(0, 2);
    std::string stem = hash.substr(2);

    for (const auto& ext : {"wv", "bin"}) {
        fs::path p = dir / (stem + "." + ext);
        if (fs::exists(p))
            return p.string();
    }
    return "";
}

bool exists(const std::string& root, const std::string& hash)
{
    return !find_blob(root, hash).empty();
}

std::string ingest(const std::string& root, const std::string& file_path)
{
    std::string h = hash::file(file_path);
    if (h.empty()) return "";

    if (exists(root, h))
        return h; // already stored, dedup

    // Try WAV compression
    wav::SplitInfo info;
    bool is_wav = wav::analyse(file_path, info);

    std::string dst;
    if (is_wav) {
        dst = blob_path(root, h, "wv");
    } else {
        dst = blob_path(root, h, "bin");
    }

    // Ensure parent directory exists
    fs::create_directories(fs::path(dst).parent_path());

    if (is_wav) {
        if (!codec::compress(file_path, info, dst)) {
            std::cerr << "store: compression failed for " << file_path << std::endl;
            return "";
        }
    } else {
        // Copy file as-is
        std::error_code ec;
        fs::copy_file(file_path, dst, ec);
        if (ec) {
            std::cerr << "store: copy failed: " << ec.message() << std::endl;
            return "";
        }
    }

    return h;
}

bool extract(const std::string& root, const std::string& hash,
             const std::string& output_path)
{
    std::string src = find_blob(root, hash);
    if (src.empty()) {
        std::cerr << "store: blob not found for " << hash << std::endl;
        return false;
    }

    fs::create_directories(fs::path(output_path).parent_path());

    // WavPack blob → decompress
    if (src.size() >= 3 && src.substr(src.size() - 3) == ".wv") {
        return codec::decompress(src, output_path);
    }

    // Binary blob → copy
    std::error_code ec;
    fs::copy_file(src, output_path,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "store: copy failed: " << ec.message() << std::endl;
        return false;
    }
    return true;
}

} // namespace store
} // namespace audiovcs
