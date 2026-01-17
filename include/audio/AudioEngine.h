#pragma once

#include "audio/OpusWrapper.h"
#include "audio/UdpSender.h"
#include "audio/RingBuffer.h"
#include <portaudio.h>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>

class AudioEngine {
public:
    AudioEngine(const std::string& host, int port);
    ~AudioEngine();

    void start();
    void stop();

private:
    static int paCallback(const void* inputBuffer, void* outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void* userData);

    void captureThread();
    void senderThread();

    RingBuffer<std::vector<int16_t>> ring_buffer_;
    OpusWrapper opus_wrapper_;
    UdpSender udp_sender_;
    PaStream* stream_;
    std::thread capture_thread_;
    std::thread sender_thread_;
    std::atomic<bool> running_;
    uint32_t sequence_number_ = 0;
};
