cmake_minimum_required(VERSION 3.15)
project(echomesh_server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)

# Find Protobuf and gRPC
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

# Fetch spdlog
include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.12.0
)
FetchContent_MakeAvailable(spdlog)

# Fetch gflags
FetchContent_Declare(
  gflags
  GIT_REPOSITORY https://github.com/gflags/gflags.git
  GIT_TAG        v2.2.2
)
set(GFLAGS_BUILD_TESTING OFF CACHE INTERNAL "")
set(GFLAGS_BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(gflags)

# Add include directories
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${GRPC_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/audio)
include_directories(${OPUS_INCLUDE_DIRS})

# --- Manual Protobuf & gRPC Generation ---

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/message.proto)
set(PROTO_CC ${CMAKE_CURRENT_BINARY_DIR}/message.pb.cc)
set(PROTO_H ${CMAKE_CURRENT_BINARY_DIR}/message.pb.h)
set(GRPC_CC ${CMAKE_CURRENT_BINARY_DIR}/message.grpc.pb.cc)
set(GRPC_H ${CMAKE_CURRENT_BINARY_DIR}/message.grpc.pb.h)

add_custom_command(
    OUTPUT ${PROTO_CC} ${PROTO_H} ${GRPC_CC} ${GRPC_H}
    COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
    ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --grpc_cpp_out=${CMAKE_CURRENT_BINARY_DIR} --plugin=protoc-gen-grpc_cpp=/usr/bin/grpc_cpp_plugin -I${CMAKE_CURRENT_SOURCE_DIR}/proto ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Manually running protoc/grpc compiler on ${PROTO_FILE}"
)

add_custom_target(generate_proto DEPENDS ${PROTO_CC} ${PROTO_H} ${GRPC_CC} ${GRPC_H})

set(SOURCE_FILES
    src/main.cpp
    src/UserManager.cpp
    src/RoomManager.cpp
    src/DatabaseManager.cpp
    src/EchoMeshServiceImpl.cpp
    ${PROTO_CC}
    ${GRPC_CC}
)

add_executable(echomesh_server ${SOURCE_FILES})
add_dependencies(echomesh_server generate_proto)

# Link libraries - using 'uuid' directly
target_link_libraries(echomesh_server PRIVATE 
    pthread 
    gRPC::grpc++
    ${PROTOBUF_LIBRARIES}
    ${OPUS_LIBRARIES}
    uuid
    asound
    gflags
    spdlog::spdlog
    mysqlclient
)
