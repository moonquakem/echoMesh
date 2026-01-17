#pragma once

#include <opus/opus.h>
#include <vector>
#include <cstdint>
#include <span>

class OpusWrapper {
public:
    OpusWrapper(int sample_rate, int channels);
    ~OpusWrapper();

    std::vector<uint8_t> encode(std::span<const int16_t> pcm);
    std::vector<int16_t> decode(std::span<const uint8_t> opus_data);

private:
    OpusEncoder* encoder_;
    OpusDecoder* decoder_;
    int channels_;
    int sample_rate_;
};
