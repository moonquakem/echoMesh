#include "DatabaseManager.h"
#include <spdlog/spdlog.h>
#include <iostream>

DatabaseManager::DatabaseManager() : conn_(nullptr) {}

DatabaseManager::~DatabaseManager() {
    if (conn_) {
        mysql_close(conn_);
    }
}

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::init(const std::string& host, const std::string& user, const std::string& password, const std::string& db, unsigned int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        spdlog::error("mysql_init failed");
        return false;
    }

    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), password.c_str(), db.c_str(), port, nullptr, 0)) {
        spdlog::error("mysql_real_connect failed: {}", mysql_error(conn_));
        mysql_close(conn_);
        conn_ = nullptr;
        return false;
    }

    spdlog::info("Connected to MySQL database: {}", db);
    return true;
}

bool DatabaseManager::execute(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!conn_) return false;
    
    if (mysql_query(conn_, sql.c_str())) {
        spdlog::error("mysql_query failed: {}", mysql_error(conn_));
        return false;
    }
    return true;
}

std::optional<DatabaseManager::UserRecord> DatabaseManager::getUser(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!conn_) return std::nullopt;

    // Use prepared statement or at least escape input to prevent SQL injection
    char escaped_user[2 * username.length() + 1];
    mysql_real_escape_string(conn_, escaped_user, username.c_str(), username.length());

    std::string sql = "SELECT id, username, password_hash FROM users WHERE username = '";
    sql += escaped_user;
    sql += "' LIMIT 1";

    if (mysql_query(conn_, sql.c_str())) {
        spdlog::error("mysql_query failed: {}", mysql_error(conn_));
        return std::nullopt;
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<UserRecord> user_opt = std::nullopt;
    if (row) {
        UserRecord rec;
        rec.id = std::stoll(row[0]);
        rec.username = row[1];
        rec.password_hash = row[2];
        user_opt = rec;
    }
    mysql_free_result(res);
    return user_opt;
}

bool DatabaseManager::createUser(const std::string& username, const std::string& password_hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!conn_) return false;

    char esc_user[2 * username.length() + 1];
    mysql_real_escape_string(conn_, esc_user, username.c_str(), username.length());
    
    char esc_pass[2 * password_hash.length() + 1];
    mysql_real_escape_string(conn_, esc_pass, password_hash.c_str(), password_hash.length());

    std::string sql = "INSERT INTO users (username, password_hash) VALUES ('";
    sql += esc_user;
    sql += "', '";
    sql += esc_pass;
    sql += "')";

    if (mysql_query(conn_, sql.c_str())) {
        spdlog::error("Failed to create user: {}", mysql_error(conn_));
        return false;
    }
    return true;
}
