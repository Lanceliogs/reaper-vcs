#include "wavpack_utils.h"

#include <cstring>
#include <iostream>

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

static int s_writeBlock(void* id, void* data, int32_t count)
{
    std::ostream *dst = static_cast<std::ostream*>(id);
    if (count < 0) return 0;
    dst->write((char*)data, count);
    return dst->good() ? 1 : 0;
}

static int32_t s_pcmSample(const uint8_t* p, size_t bytes)
{
    uint32_t value = u32LE(p, bytes);
    const unsigned bits = static_cast<unsigned>(bytes * 8);

    if (bits < 32 && (value & (uint32_t{1} << (bits - 1))))
        value |= (~uint32_t{0} << bits);

    return static_cast<int32_t>(value);
}

WavpackEncoder::WavpackEncoder(std::ostream& dst)
    : m_dst(dst)
{
    std::memset(&m_cfg, 0, sizeof(m_cfg));
    m_ctx = WavpackOpenFileOutput(s_writeBlock, &m_dst, nullptr);
    m_valid = (m_ctx != nullptr);
}

WavpackEncoder::~WavpackEncoder()
{
    if (m_ctx)
    {
        WavpackCloseFile(m_ctx);
        m_ctx = nullptr;
    }

    m_valid = false;
}

bool WavpackEncoder::configure(const WaveHeader& fmt, uint64_t dataSize)
{
    WavpackConfig cfg{};
    cfg.num_channels = fmt.NumChannels;
    cfg.sample_rate = fmt.SampleRate;
    cfg.bits_per_sample = fmt.BitsPerSample;
    cfg.bytes_per_sample = (fmt.BitsPerSample + 7) / 8;
    cfg.channel_mask = fmt.ChannelMask;
    return configure(cfg, dataSize, fmt.BlockAlign);
}

bool WavpackEncoder::configure(const WavpackConfig& cfg, uint64_t dataSize, int blockAlign)
{
    if (m_ctx == nullptr || blockAlign <= 0 || cfg.num_channels <= 0) {
        m_valid = false;
        return false;
    }
    m_cfg = cfg;
    m_dataSize = dataSize;
    m_blockAlign = blockAlign;
    uint64_t totalSamples = dataSize / blockAlign;
    m_valid = WavpackSetConfiguration64(m_ctx, &m_cfg, totalSamples, nullptr) && WavpackPackInit(m_ctx);
    return m_valid;
}

bool WavpackEncoder::encode(void *bytes, size_t count)
{
    if (!m_valid) {
        std::cout << "Invalid state" << std::endl;
        return false;
    }
    if (count % m_blockAlign != 0) {
        std::cout << "Bad align" << std::endl;
        return false;
    }
    
    const size_t sampleCount = count / m_blockAlign;
    const size_t bufferSize = sampleCount * m_cfg.num_channels;
    const int width = m_blockAlign / m_cfg.num_channels;

    if (m_buf.size() < bufferSize)
        m_buf.resize(bufferSize);
    
    for (size_t i = 0; i < bufferSize; ++i)
        m_buf[i] = s_pcmSample((uint8_t*)bytes + i * width, width);

    return WavpackPackSamples(m_ctx, m_buf.data(), sampleCount);
}

bool WavpackEncoder::flush()
{
    if (!m_valid) return false;
    return WavpackFlushSamples(m_ctx);
}

bool WavpackEncoder::good() const
{
    return m_valid;
}

bool WavpackEncoder::bad() const
{
    return !m_valid;
}