cmake_minimum_required(VERSION 3.10)
project(echomesh_server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find PkgConfig
find_package(PkgConfig REQUIRED)

# Find Opus
pkg_check_modules(OPUS REQUIRED opus)

# Find PortAudio
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# Find Protobuf
find_package(Protobuf REQUIRED)

# Add protobuf include directory
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Create a directory for proto files if it doesn't exist
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/proto)

# Define the proto file
set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/message.proto)

# Define the output variables for generated files
set(PROTO_SRCS)
set(PROTO_HDRS)

# Generate C++ sources and headers from the proto file
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})

# Add the generated files to the source list, making sure they are in the build directory
set(PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR}/message.pb.cc)
set(PROTO_HDRS ${CMAKE_CURRENT_BINARY_DIR}/message.pb.h)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/audio)
include_directories(${OPUS_INCLUDE_DIRS})
include_directories(${PORTAUDIO_INCLUDE_DIRS})

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
    src/MsgDispatcher.cpp
    src/UserManager.cpp
    src/RoomManager.cpp
    src/BusinessLogic.cpp
    src/EventLoopThread.cpp
    src/audio/OpusWrapper.cpp
    src/audio/UdpSender.cpp
    src/audio/JitterBuffer.cpp
    src/audio/AudioEngine.cpp
    ${PROTO_SRCS}
)

add_executable(echomesh_server ${SOURCE_FILES})

target_link_libraries(echomesh_server PRIVATE 
    pthread 
    ${PROTOBUF_LIBRARIES}
    ${OPUS_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    asound
)
