#include "TcpServer.h"
#include "EventLoop.h"
#include "TcpConnection.h"
#include <iostream>
#include <netinet/in.h>

#include "TcpServer.h"
#include "EventLoop.h"
#include "TcpConnection.h"
#include <iostream>
#include <netinet/in.h>

void onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        std::cout << "New connection " << conn->name() << " from "
                  // Simplified- you would need a utility to convert IP to string
                  << "some-address" << std::endl;
    } else {
        std::cout << "Connection " << conn->name() << " is down." << std::endl;
    }
}

void onMessage(const TcpConnectionPtr& conn, Buffer* buf) {
    std::string msg(buf->retrieveAllAsString());
    std::cout << "Received " << msg.size() << " bytes from " << conn->name() << std::endl;
    // Echo the message back
    conn->send(msg);
}

int main() {
    std::cout << "Starting EchoMesh server..." << std::endl;

    EventLoop loop;
    
    sockaddr_in listenAddr;
    listenAddr.sin_family = AF_INET;
    listenAddr.sin_addr.s_addr = INADDR_ANY;
    listenAddr.sin_port = htons(8888);

    // Create a server with 4 worker threads in the pool
    TcpServer server(&loop, listenAddr, 4);

    server.setConnectionCallback(onConnection);
    server.setMessageCallback(onMessage);

    server.start();
    loop.loop(); // Start the main event loop

    return 0;
}
