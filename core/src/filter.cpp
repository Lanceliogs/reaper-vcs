#include "audiovcs/filter.h"
#include "audiovcs/hash.h"
#include "audiovcs/pointer.h"
#include "audiovcs/store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace audiovcs {
namespace filter {

static std::string find_store_root()
{
    fs::path dir = fs::current_path();
    while (true) {
        fs::path candidate = dir / store::STORE_DIR;
        if (fs::is_directory(candidate))
            return candidate.string();
        if (!dir.has_parent_path() || dir == dir.parent_path())
            break;
        dir = dir.parent_path();
    }
    return "";
}

int clean(const std::string& path)
{
    std::string root = find_store_root();
    if (root.empty()) {
        std::cerr << "audiovcs: not initialised (run 'audiovcs install')"
                  << std::endl;
        return 1;
    }

    // Check if the file is already a pointer (idempotent)
    {
        std::ifstream f(path, std::ios::binary);
        char peek[32] = {0};
        f.read(peek, sizeof(peek));
        if (pointer::is_pointer(peek, f.gcount())) {
            // Already a pointer — pass through unchanged
            f.seekg(0);
            std::cout << f.rdbuf();
            return 0;
        }
    }

    // Get original file size
    std::error_code ec;
    uint64_t size = fs::file_size(path, ec);
    if (ec) {
        std::cerr << "audiovcs: cannot stat " << path << std::endl;
        return 1;
    }

    // Ingest into store
    std::string h = store::ingest(root, path);
    if (h.empty()) return 1;

    // Write pointer to stdout (this is what git stores)
    pointer::Pointer ptr{h, size};
    pointer::write(std::cout, ptr);
    return 0;
}

int smudge(const std::string& path)
{
    // Read pointer from stdin
    pointer::Pointer ptr;
    if (!pointer::read(std::cin, ptr)) {
        // Not a pointer — pass stdin through unchanged
        std::cin.clear();
        std::cin.seekg(0);
        std::cout << std::cin.rdbuf();
        return 0;
    }

    std::string root = find_store_root();
    if (root.empty()) {
        std::cerr << "audiovcs: not initialised (run 'audiovcs install')"
                  << std::endl;
        // Write the pointer as-is so the file isn't corrupted
        pointer::write(std::cout, ptr);
        return 1;
    }

    // Extract from store to a temp file, then stream to stdout
    fs::path tmp = fs::temp_directory_path() / ("audiovcs_smudge_" + ptr.hash);
    if (!store::extract(root, ptr.hash, tmp.string())) {
        // Blob missing — write pointer so user can see something
        pointer::write(std::cout, ptr);
        return 1;
    }

    std::ifstream f(tmp, std::ios::binary);
    std::cout << f.rdbuf();
    f.close();
    fs::remove(tmp);
    return 0;
}

} // namespace filter
} // namespace audiovcs
