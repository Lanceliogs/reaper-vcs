#pragma once

#include <wavpack.h>

#include <istream>
#include <ostream>
#include <cstdint>
#include <memory>
#include <vector>

class WavpackEncoder
{
public:
    WavpackEncoder(std::ostream& dst);
    ~WavpackEncoder();

    bool configure(const WaveHeader& fmt, uint64_t dataSize);
    bool configure(const WavpackConfig& cfg, uint64_t dataSize, int blockAlign);

    bool encode(void *bytes, size_t count);
    bool flush();

    bool good() const;
    bool bad() const;

private:
    std::ostream &m_dst;
    WavpackConfig m_cfg;
    WavpackContext *m_ctx;

    uint64_t m_dataSize;
    int m_blockAlign;
    std::vector<int32_t> m_buf;

    bool m_valid;
};

class WavpackDecoder
{

};