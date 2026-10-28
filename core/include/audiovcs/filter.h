#pragma once

#include <string>

namespace audiovcs {
namespace filter {

// Clean filter: called by git during staging.
// Reads the audio file at `path`, stores it in the object store,
// and writes a pointer to stdout.
int clean(const std::string& path);

// Smudge filter: called by git during checkout.
// Reads a pointer from stdin, retrieves the blob from the store,
// and writes the restored audio to stdout.
int smudge(const std::string& path);

} // namespace filter
} // namespace audiovcs
