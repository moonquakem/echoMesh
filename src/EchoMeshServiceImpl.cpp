#include "EchoMeshServiceImpl.h"
#include <iostream>
#include <memory>
#include <string>
#include <uuid/uuid.h> // For generating tokens

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
    
    std::cout << "RPC: Login for user '" << request->username() << "'" << std::endl;

    // In a real app, you'd verify the password. Here we just log in.
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

    std::cout << "RPC: ManageRoom for user " << userId << " in room '" << request->room_id() << "'" << std::endl;

    switch (request->action_type()) {
        case echomesh::RA_CREATE_OR_JOIN:
            if (m_roomManager.joinRoom(request->room_id(), userId)) {
                // This was the missing piece: update the UserManager as well.
                m_userManager.joinRoom(userId, request->room_id());
                
                response->set_status_code(echomesh::SC_OK);
                response->set_message("Joined room successfully.");
                std::cout << "User " << userId << " joined room " << request->room_id() << std::endl;
            } else {
                response->set_status_code(echomesh::SC_ERROR);
                response->set_message("Failed to join room.");
                std::cout << "User " << userId << " failed to join room " << request->room_id() << std::endl;
            }
            break;
        case echomesh::RA_LEAVE:
            m_roomManager.leaveRoom(request->room_id(), userId);
            response->set_status_code(echomesh::SC_OK);
            response->set_message("Left room successfully.");
            std::cout << "User " << userId << " left room " << request->room_id() << std::endl;
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
    
    std::cout << "RPC: StreamAudio started for user " << userId << " in room " << roomId << std::endl;

    // This is the most complex part. We need to register this user's stream
    // with the RoomManager so other users can send audio to it.
    // And we need a loop to read this user's audio and forward it.
    
    // For now, this is a placeholder implementation.
    // In the real implementation, we will modify RoomManager to handle the streams.

    m_roomManager.addAudioStream(roomId, userId, stream);

    echomesh::VoicePacket received_packet;
    while (stream->Read(&received_packet)) {
        // The user sent us a packet. We need to forward it.
        // We'll add the user ID to the packet before forwarding.
        received_packet.set_user_id(userId);
        m_roomManager.broadcastAudio(roomId, userId, received_packet);
    }

    // The stream has ended (client disconnected).
    std::cout << "RPC: StreamAudio ended for user " << userId << std::endl;
    m_roomManager.removeAudioStream(roomId, userId);

    return grpc::Status::OK;
}


UserId EchoMeshServiceImpl::getUserIdFromContext(grpc::ServerContext* context) {
    const auto& metadata = context->client_metadata();
    auto token_iter = metadata.find("session-token");

    if (token_iter == metadata.end()) {
        std::cerr << "Authentication Error: No session-token in metadata." << std::endl;
        return 0; // Invalid UserId
    }

    std::string token(token_iter->second.data(), token_iter->second.length());
    UserId userId = m_userManager.getUserIdByToken(token);

    if (userId == 0) {
        std::cerr << "Authentication Error: Invalid token '" << token << "'" << std::endl;
    }

    return userId;
}
