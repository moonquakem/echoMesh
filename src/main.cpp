#include "EchoMeshServiceImpl.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

DEFINE_string(host, "0.0.0.0", "Server host address");
DEFINE_int32(port, 8888, "Server port");
DEFINE_int32(max_threads, 64, "Max threads for audio broadcasting");
DEFINE_int32(max_pending_packets, 5000, "Max pending packets before dropping");
DEFINE_string(log_level, "info", "Log level (trace, debug, info, warn, err, critical, off)");

void SetLogLevel(const std::string& level) {
    if (level == "trace") spdlog::set_level(spdlog::level::trace);
    else if (level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (level == "info") spdlog::set_level(spdlog::level::info);
    else if (level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (level == "err") spdlog::set_level(spdlog::level::err);
    else if (level == "critical") spdlog::set_level(spdlog::level::critical);
    else if (level == "off") spdlog::set_level(spdlog::level::off);
    else {
        spdlog::warn("Unknown log level '{}', defaulting to 'info'", level);
        spdlog::set_level(spdlog::level::info);
    }
}

void RunServer() {
    std::string server_address = FLAGS_host + ":" + std::to_string(FLAGS_port);
    EchoMeshServiceImpl service;

    grpc::ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case, it corresponds to an *synchronous* service.
    builder.RegisterService(&service);

    // Finally assemble the server.
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        spdlog::critical("Failed to start gRPC server on {}", server_address);
        return;
    }
    spdlog::info("Server listening on {}", server_address);

    // Wait for the server to shut down. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

int main(int argc, char** argv) {
    gflags::SetUsageMessage("Usage: echomesh_server [options]");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
    SetLogLevel(FLAGS_log_level);

    spdlog::info("Starting EchoMesh Server...");
    spdlog::info("Config: host={}, port={}, max_threads={}, max_pending_packets={}, log_level={}",
                 FLAGS_host, FLAGS_port, FLAGS_max_threads, FLAGS_max_pending_packets, FLAGS_log_level);

    try {
        RunServer();
    } catch (const std::exception& e) {
        spdlog::error("Unhandled exception: {}", e.what());
        return 1;
    }
    
    spdlog::info("Server shutting down.");
    gflags::ShutDownCommandLineFlags();
    return 0;
}
