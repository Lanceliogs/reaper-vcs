#pragma once

#include <string>

namespace audiovcs {
namespace store {

// Default store directory name
constexpr const char* STORE_DIR = ".audiovcs";

// Construct the blob path for a given hash.
// Layout: <root>/objects/<first 2 hex chars>/<remaining hex>.<ext>
std::string blob_path(const std::string& root, const std::string& hash,
                      const std::string& ext);

// Check whether a blob for the given hash already exists.
bool exists(const std::string& root, const std::string& hash);

// Ingest an audio file into the store.
// WAV files are compressed with WavPack; other formats are stored as-is.
// Returns the SHA-256 hash on success, or empty string on error.
std::string ingest(const std::string& root, const std::string& file_path);

// Extract a blob from the store to the given output path.
// WavPack blobs are decompressed; other blobs are copied verbatim.
bool extract(const std::string& root, const std::string& hash,
             const std::string& output_path);

} // namespace store
} // namespace audiovcs
