#include "BusinessLogic.h"
#include "EventLoop.h"
#include "TcpServer.h"
#include "UdpServer.h"
#include "ThreadPool.h"
#include <iostream>

int main() {
    try {
        EventLoop loop;
        TcpServer tcp_server(&loop, 8888, 4);
        UdpServer udp_server(&loop, 9999);

        registerBusinessLogicHandlers();

        tcp_server.start();
        udp_server.start();

        std::cout << "EchoMesh server running..." << std::endl;

        loop.loop();

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
