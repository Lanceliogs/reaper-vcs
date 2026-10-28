#pragma once

#include "wav.h"

#include <string>

namespace audiovcs {
namespace codec {

// Compress a WAV file into a standard WavPack (.wv) file with embedded
// RIFF wrappers. The wrappers enable byte-for-byte reconstruction.
bool compress(const std::string& wav_path, const wav::SplitInfo& info,
              const std::string& wv_path);

// Decompress a WavPack file back to the original WAV.
// Uses the embedded wrappers for byte-for-byte reconstruction.
bool decompress(const std::string& wv_path, const std::string& wav_path);

} // namespace codec
} // namespace audiovcs
