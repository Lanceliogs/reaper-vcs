#pragma once

#include <string>

namespace flac {
    
int wav2flac(const std::string& wav, const std::string& flac);
int flac2wav(const std::string& flac, const std::string& wav);

};