#pragma once

#include <mysql/mysql.h>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <optional>

class DatabaseManager {
public:
    static DatabaseManager& getInstance();

    bool init(const std::string& host, const std::string& user, const std::string& password, const std::string& db, unsigned int port = 3306);
    
    // Execute non-query SQL (INSERT, UPDATE, DELETE)
    bool execute(const std::string& sql);
    
    // Simple wrapper for fetching a single user's password hash and ID
    struct UserRecord {
        int64_t id;
        std::string username;
        std::string password_hash;
    };
    std::optional<UserRecord> getUser(const std::string& username);
    
    // Create a new user
    bool createUser(const std::string& username, const std::string& password_hash);

    ~DatabaseManager();

private:
    DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    MYSQL* conn_;
    std::mutex mutex_;
};
