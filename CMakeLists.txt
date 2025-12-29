cmake_minimum_required(VERSION 3.20)
project(EchoMesh CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

set(SOURCE_FILES
    src/main.cpp
    src/Buffer.cpp
    src/ThreadPool.cpp
    src/EpollPoller.cpp
    src/Channel.cpp
    src/EventLoop.cpp
    src/Acceptor.cpp
    src/TcpServer.cpp
    src/TcpConnection.cpp
)

include_directories(include)

add_executable(echomesh_server ${SOURCE_FILES})

target_link_libraries(echomesh_server PRIVATE pthread)
