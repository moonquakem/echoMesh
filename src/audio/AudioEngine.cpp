#include "audio/AudioEngine.h"
#include <iostream>
#include <stdexcept>

namespace {
    constexpr int SAMPLE_RATE = 48000;
    constexpr int CHANNELS = 1;
    constexpr int FRAMES_PER_BUFFER = 960; // 20ms at 48kHz
    constexpr int RING_BUFFER_CAPACITY = 100;
}

AudioEngine::AudioEngine(const std::string& host, int port)
    : ring_buffer_(RING_BUFFER_CAPACITY),
      opus_wrapper_(SAMPLE_RATE, CHANNELS),
      udp_sender_(host, port),
      stream_(nullptr),
      running_(false) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        throw std::runtime_error("PortAudio initialization failed: " + std::string(Pa_GetErrorText(err)));
    }
}

AudioEngine::~AudioEngine() {
    if (running_) {
        stop();
    }
    Pa_Terminate();
}

void AudioEngine::start() {
    running_ = true;

    PaError err = Pa_OpenDefaultStream(&stream_, CHANNELS, 0, paInt16, SAMPLE_RATE,
                                     FRAMES_PER_BUFFER, paCallback, this);
    if (err != paNoError) {
        throw std::runtime_error("PortAudio failed to open stream: " + std::string(Pa_GetErrorText(err)));
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        throw std::runtime_error("PortAudio failed to start stream: " + std::string(Pa_GetErrorText(err)));
    }

    sender_thread_ = std::thread(&AudioEngine::senderThread, this);
}

void AudioEngine::stop() {
    running_ = false;

    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }

    if (sender_thread_.joinable()) {
        sender_thread_.join();
    }
}

int AudioEngine::paCallback(const void* inputBuffer, void* outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void* userData) {
    AudioEngine* self = static_cast<AudioEngine*>(userData);
    const int16_t* pcm = static_cast<const int16_t*>(inputBuffer);
    
    if (inputBuffer != nullptr) {
        std::vector<int16_t> pcm_data(pcm, pcm + framesPerBuffer * CHANNELS);
        self->ring_buffer_.push(pcm_data);
    }
    
    return paContinue;
}

void AudioEngine::senderThread() {
    while (running_) {
        auto pcm_data = ring_buffer_.pop();
        if (pcm_data) {
            try {
                std::vector<uint8_t> opus_data = opus_wrapper_.encode(*pcm_data);
                VoicePacket packet;
                packet.sequence = sequence_number_++;
                packet.timestamp = Pa_GetStreamTime(stream_);
                packet.userId = 0; // Or some other user ID
                packet.data = opus_data;
                udp_sender_.send(packet);
            } catch (const std::exception& e) {
                std::cerr << "Error in sender thread: " << e.what() << std::endl;
            }
        }
    }
}
