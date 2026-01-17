#include "audio/OpusWrapper.h"
#include <stdexcept>

namespace {
    constexpr int FRAME_SIZE = 960;
    constexpr int MAX_PACKET_SIZE = 4000;
}

OpusWrapper::OpusWrapper(int sample_rate, int channels)
    : channels_(channels), sample_rate_(sample_rate) {
    int error;
    encoder_ = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        throw std::runtime_error("Failed to create Opus encoder: " + std::string(opus_strerror(error)));
    }

    decoder_ = opus_decoder_create(sample_rate, channels, &error);
    if (error != OPUS_OK) {
        throw std::runtime_error("Failed to create Opus decoder: " + std::string(opus_strerror(error)));
    }
}

OpusWrapper::~OpusWrapper() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
    }
    if (decoder_) {
        opus_decoder_destroy(decoder_);
    }
}

std::vector<uint8_t> OpusWrapper::encode(std::span<const int16_t> pcm) {
    if (pcm.size() != FRAME_SIZE * channels_) {
        throw std::invalid_argument("Invalid PCM data size for encoding");
    }

    std::vector<uint8_t> opus_data(MAX_PACKET_SIZE);
    opus_int32 len = opus_encode(encoder_, pcm.data(), FRAME_SIZE, opus_data.data(), opus_data.size());

    if (len < 0) {
        throw std::runtime_error("Opus encoding failed: " + std::string(opus_strerror(len)));
    }

    opus_data.resize(len);
    return opus_data;
}

std::vector<int16_t> OpusWrapper::decode(std::span<const uint8_t> opus_data) {
    std::vector<int16_t> pcm(FRAME_SIZE * channels_);
    int num_samples = opus_decode(decoder_, opus_data.data(), opus_data.size(), pcm.data(), FRAME_SIZE, 0);

    if (num_samples < 0) {
        throw std::runtime_error("Opus decoding failed: " + std::string(opus_strerror(num_samples)));
    }

    pcm.resize(num_samples * channels_);
    return pcm;
}
