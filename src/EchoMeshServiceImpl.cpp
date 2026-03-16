#include "EchoMeshServiceImpl.h"
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <uuid/uuid.h> 
#include <grpcpp/support/proto_buffer_writer.h>
#include <grpcpp/support/proto_buffer_reader.h>

// Helper to generate a random session token
std::string generate_token() {
    uuid_t uuid;
    uuid_generate_random(uuid);
    char token[37];
    uuid_unparse_lower(uuid, token);
    return std::string(token);
}

EchoMeshServiceImpl::EchoMeshServiceImpl()
    : m_userManager(UserManager::getInstance()),
      m_roomManager(RoomManager::getInstance()) {}

grpc::Status EchoMeshServiceImpl::Login(
    grpc::ServerContext* context, 
    const echomesh::LoginRequest* request, 
    echomesh::LoginResponse* response) {
    
    spdlog::info("RPC: Login for user '{}'", request->username());

    std::string token = generate_token();
    UserId userId = m_userManager.login(request->username(), token);

    if (userId > 0) {
        response->set_status_code(echomesh::SC_OK);
        response->set_message("Login successful");
        response->set_user_id(userId);
        response->set_session_token(token);
        return grpc::Status::OK;
    } else {
        response->set_status_code(echomesh::SC_ERROR);
        response->set_message("Login failed (user may already be logged in).");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Login failed");
    }
}

grpc::Status EchoMeshServiceImpl::ManageRoom(
    grpc::ServerContext* context, 
    const echomesh::RoomActionRequest* request, 
    echomesh::RoomActionResponse* response) {

    UserId userId = getUserIdFromContext(context);
    if (userId == 0) {
        response->set_status_code(echomesh::SC_UNAUTHENTICATED);
        response->set_message("Invalid or missing session token.");
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, response->message());
    }

    spdlog::info("RPC: ManageRoom for user {} in room '{}'", userId, request->room_id());

    switch (request->action_type()) {
        case echomesh::RA_CREATE_OR_JOIN:
            if (m_roomManager.joinRoom(request->room_id(), userId)) {
                m_userManager.joinRoom(userId, request->room_id());
                response->set_status_code(echomesh::SC_OK);
                response->set_message("Joined room successfully.");
            } else {
                response->set_status_code(echomesh::SC_ERROR);
                response->set_message("Failed to join room.");
            }
            break;
        case echomesh::RA_LEAVE:
            m_roomManager.leaveRoom(request->room_id(), userId);
            response->set_status_code(echomesh::SC_OK);
            response->set_message("Left room successfully.");
            break;
        default:
            response->set_status_code(echomesh::SC_ERROR);
            response->set_message("Unknown room action.");
            break;
    }
    
    return grpc::Status::OK;
}

grpc::Status EchoMeshServiceImpl::StreamAudio(
    grpc::ServerContext* context, 
    grpc::ServerReaderWriter<echomesh::VoicePacket, echomesh::VoicePacket>* stream) {
    
    UserId userId = getUserIdFromContext(context);
    if (userId == 0) {
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Invalid or missing session token.");
    }

    std::string roomId = m_userManager.getRoomId(userId);
    if (roomId.empty()) {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "User is not in a room.");
    }
    
    spdlog::info("RPC: StreamAudio started for user {} in room {}", userId, roomId);

    // HACK: Cast the typed stream to a ByteBuffer stream.
    // This works in gRPC C++ because the wire format is determined by the serialized bytes.
    // This allows us to use pre-serialized buffers for broadcast.
    auto byte_stream = reinterpret_cast<AudioStream*>(stream);

    m_roomManager.addAudioStream(roomId, userId, byte_stream);

    grpc::ByteBuffer buffer;
    while (byte_stream->Read(&buffer)) {
        echomesh::VoicePacket packet;
        bool own_buffer = false;
        
        // Deserialize the received buffer into a Packet
        grpc::Status status = grpc::GenericDeserialize<grpc::ProtoBufferReader, echomesh::VoicePacket>(
            &buffer, &packet
        );

        if (status.ok()) {
            packet.set_user_id(userId);
            m_roomManager.broadcastAudio(roomId, userId, packet);
        } else {
            spdlog::error("Failed to deserialize VoicePacket from user {}", userId);
        }
        buffer.Clear();
    }

    spdlog::info("RPC: StreamAudio ended for user {}", userId);
    m_roomManager.removeAudioStream(roomId, userId);

    return grpc::Status::OK;
}


UserId EchoMeshServiceImpl::getUserIdFromContext(grpc::ServerContext* context) {
    const auto& metadata = context->client_metadata();
    auto token_iter = metadata.find("session-token");

    if (token_iter == metadata.end()) {
        spdlog::error("Authentication Error: No session-token in metadata.");
        return 0; 
    }

    std::string token(token_iter->second.data(), token_iter->second.length());
    UserId userId = m_userManager.getUserIdByToken(token);

    if (userId == 0) {
        spdlog::error("Authentication Error: Invalid token '{}'", token);
    }

    return userId;
}
