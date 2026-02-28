#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

using UserId = int64_t;
using RoomId = std::string;
using Token = std::string;

// Holds information about a logged-in user
struct User {
    UserId id;
    std::string username;
    RoomId current_room;
    Token token;
};

class UserManager {
public:
    static UserManager &getInstance();

    // Creates a user session with a token and returns the new user's ID
    UserId login(const std::string &username, const Token &token);

    // Removes a user session
    void logout(UserId userId);

    // Finds a user ID by their session token
    UserId getUserIdByToken(const Token &token);

    // Gets the room ID for a user
    RoomId getRoomId(UserId userId);
    
    // Updates a user's current room
    void joinRoom(UserId userId, const RoomId& roomId);
    void leaveRoom(UserId userId);

private:
    UserManager() = default;
    ~UserManager() = default;
    UserManager(const UserManager &) = delete;
    UserManager &operator=(const UserManager &) = delete;

    std::mutex mutex_;

    // Main mapping from UserId to user details
    std::unordered_map<UserId, User> users_;
    
    // Index for quick lookup from token to UserId
    std::unordered_map<Token, UserId> token_to_user_;

    UserId next_user_id_ = 1;
};
