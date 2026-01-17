#include "BusinessLogic.h"
#include "EventLoop.h"
#include "TcpServer.h"
#include "ThreadPool.h"
#include "audio/AudioEngine.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    try {
        EventLoop loop;
        TcpServer server(&loop, 8888, 4);

        registerBusinessLogicHandlers();

        server.start();

        std::thread control_thread([&]() {
            // Start the audio engine
            AudioEngine audio_engine("127.0.0.1", 12345);
            audio_engine.start();

            std::cout << "Server and AudioEngine running for 60 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(60));
            
            audio_engine.stop();
            loop.quit();
        });

        loop.loop();
        control_thread.join();

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
