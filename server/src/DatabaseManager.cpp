#include "DatabaseManager.h"
#include "Utils.h"
#include <iostream>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

DatabaseManager::DatabaseManager(const std::string &dbFile)
    : db(nullptr) {
    sqlite3_open(dbFile.c_str(), &db);
}

DatabaseManager::~DatabaseManager() {
    sqlite3_close(db);
}

bool DatabaseManager::init() {
    std::lock_guard<std::mutex> l(mtx);
    const char *sqls[] = {
        "CREATE TABLE IF NOT EXISTS Users(user_id INTEGER PRIMARY KEY, username TEXT UNIQUE, password TEXT);",
        "CREATE TABLE IF NOT EXISTS Friends(user_id INTEGER, friend_id INTEGER, is_blocked INTEGER, PRIMARY KEY(user_id,friend_id));",
        "CREATE TABLE IF NOT EXISTS Messages(msg_id INTEGER PRIMARY KEY, sender_id INTEGER, receiver_id INTEGER, group_id INTEGER, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, delivered INTEGER, FOREIGN KEY(group_id) REFERENCES Groups(group_id) ON DELETE CASCADE);",
        "CREATE TABLE IF NOT EXISTS FriendRequests(request_id INTEGER PRIMARY KEY, user_id INTEGER, friend_id INTEGER, status INTEGER);",
        "CREATE TABLE IF NOT EXISTS Groups(group_id INTEGER PRIMARY KEY AUTOINCREMENT, group_name TEXT UNIQUE);",
        "CREATE TABLE IF NOT EXISTS GroupMembers(group_id INTEGER, user_id INTEGER, "
        "FOREIGN KEY(group_id) REFERENCES Groups(group_id), "
        "FOREIGN KEY(user_id) REFERENCES Users(user_id), "
        "PRIMARY KEY(group_id, user_id));",

        "CREATE TABLE IF NOT EXISTS FileTransfers(file_id INTEGER PRIMARY KEY AUTOINCREMENT, sender_id INTEGER, receiver_id INTEGER, file_name TEXT, file_data BLOB, status INTEGER);",  // 文件传输表
        
        nullptr
    };
    for (int i = 0; sqls[i]; ++i)
        if (!execute(sqls[i])) return false;
    return true;
}

bool DatabaseManager::execute(const std::string &sql) {
    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << errmsg << std::endl;
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

bool DatabaseManager::registerUser(const std::string &u, const std::string &p) {
    std::lock_guard<std::mutex> l(mtx);
    const std::string hashed = sha256(p);
    return executePrepared("INSERT INTO Users(username,password) VALUES(?,?);", {
        {u, SQLITE_TEXT}, {hashed, SQLITE_TEXT}
    });
}

bool DatabaseManager::verifyUser(const std::string &u, const std::string &p, int &userId) {
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *st;
    std::string hashed = sha256(p);
    sqlite3_prepare_v2(db, "SELECT user_id FROM Users WHERE username=? AND password=?;", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hashed.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        userId = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return true;
    }
    sqlite3_finalize(st);
    return false;
}


bool DatabaseManager::updateUser(int id, const std::string &n, const std::string &pw) {
    std::lock_guard<std::mutex> l(mtx);
    std::string hashed = sha256(pw);
    return executePrepared("UPDATE Users SET username=?,password=? WHERE user_id=?;", {
        {n, SQLITE_TEXT}, {hashed, SQLITE_TEXT}, {std::to_string(id), SQLITE_INTEGER}
    });
}

bool DatabaseManager::deleteUser(int id) {
    std::lock_guard<std::mutex> l(mtx);
    return executePrepared("DELETE FROM Users WHERE user_id=?;", {
        {std::to_string(id), SQLITE_INTEGER}
    });
}

bool DatabaseManager::isFriendRequestExists(int u, int f) {
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *stmt = nullptr;
    
    // 检查是否已经发送过好友请求（status = 0 表示待确认），并且检查两个方向的请求
    const char *sql = "SELECT COUNT(*) FROM FriendRequests WHERE ((user_id=? AND friend_id=?) OR (user_id=? AND friend_id=?)) AND status=0;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, u);  // 当前用户 -> 目标用户
    sqlite3_bind_int(stmt, 2, f);
    sqlite3_bind_int(stmt, 3, f);  // 目标用户 -> 当前用户
    sqlite3_bind_int(stmt, 4, u);
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count > 0;  // 如果查询结果大于0，表示已经发送过请求
}

bool DatabaseManager::sendFriendRequest(int u, int f) {
    // 检查是否已经发送过请求（双向检查）
    {
        if (isFriendRequestExists(u, f)) {
            std::cout << "[ERROR] 用户 " << u << " 和用户 " << f << " 之间已经有待确认的好友请求" << std::endl;
            return false;  // 已经有待确认的请求，阻止重复发送
        }
    }
    // 如果没有发送过请求，则继续插入请求
    return executePrepared("INSERT INTO FriendRequests(user_id,friend_id,status) VALUES(?,?,0);", {
        {std::to_string(u), SQLITE_INTEGER}, {std::to_string(f), SQLITE_INTEGER}
    });
}

std::vector<FriendRequestRecord> DatabaseManager::getFriendRequests(int userId) {
    std::vector<FriendRequestRecord> list;
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db, "SELECT request_id,user_id,friend_id,status FROM FriendRequests WHERE friend_id=? AND status=0;", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, userId);
    while (sqlite3_step(st) == SQLITE_ROW) {
        FriendRequestRecord r;
        r.requestId = sqlite3_column_int(st,0);
        r.userId    = sqlite3_column_int(st,1);
        r.friendId  = sqlite3_column_int(st,2);
        r.status    = sqlite3_column_int(st,3);
        list.push_back(r);
    }
    sqlite3_finalize(st);
    return list;
}

bool DatabaseManager::respondFriendRequest(int requestId, bool accept) {
    int u = -1, f = -1;

    {
        std::lock_guard<std::mutex> l(mtx);
        // 查原始数据
        sqlite3_stmt *q;
        sqlite3_prepare_v2(db, "SELECT user_id,friend_id FROM FriendRequests WHERE request_id=?;", -1, &q, nullptr);
        sqlite3_bind_int(q, 1, requestId);
        if (sqlite3_step(q) == SQLITE_ROW) {
            u = sqlite3_column_int(q, 0);
            f = sqlite3_column_int(q, 1);
        }
        sqlite3_finalize(q);

        if (u < 0 || f < 0) return false;

        // 更新状态
        if (!executePrepared("UPDATE FriendRequests SET status=? WHERE request_id=?;", {
            {std::to_string(accept ? 1 : 2), SQLITE_INTEGER}, {std::to_string(requestId), SQLITE_INTEGER}
        })) return false;
    }

    if (!accept) return true;

    // 不持锁调用 addFriend（它自己内部加锁）
    bool ok1 = addFriend(u, f);
    std::cout << "[DEBUG] Added 1: " << ok1 << std::endl;
    bool ok2 = addFriend(f, u);
    std::cout << "[DEBUG] Added 2: " << ok2 << std::endl;
    return ok1 && ok2;
}


bool DatabaseManager::addFriend(int userId, int friendId) {
    std::cout << "[DEBUG] addFriend(" << userId << ", " << friendId << ")" << std::endl;
    std::lock_guard<std::mutex> l(mtx);
    return executePrepared("INSERT OR IGNORE INTO Friends(user_id,friend_id,is_blocked) VALUES(?,?,0);", {
        {std::to_string(userId), SQLITE_INTEGER}, {std::to_string(friendId), SQLITE_INTEGER}
    });
}

bool DatabaseManager::deleteFriend(int userId, int friendId) {
    std::lock_guard<std::mutex> l(mtx);
    return executePrepared(
        "DELETE FROM Friends WHERE (user_id=? AND friend_id=?) OR (user_id=? AND friend_id=?);",
        {
            {std::to_string(userId), SQLITE_INTEGER},
            {std::to_string(friendId), SQLITE_INTEGER},
            {std::to_string(friendId), SQLITE_INTEGER},
            {std::to_string(userId), SQLITE_INTEGER}
        }
    );
}

bool DatabaseManager::blockFriend(int userId, int friendId) {
    std::lock_guard<std::mutex> l(mtx);
    return executePrepared("UPDATE Friends SET is_blocked=1 WHERE user_id=? AND friend_id=?;", {
        {std::to_string(userId), SQLITE_INTEGER}, {std::to_string(friendId), SQLITE_INTEGER}
    });
}

bool DatabaseManager::unblockFriend(int userId, int friendId) {
    std::lock_guard<std::mutex> l(mtx);
    return executePrepared("UPDATE Friends SET is_blocked=0 WHERE user_id=? AND friend_id=?;", {
        {std::to_string(userId), SQLITE_INTEGER}, {std::to_string(friendId), SQLITE_INTEGER}
    });
}

std::vector<FriendRecord> DatabaseManager::getFriends(int userId) {
    std::vector<FriendRecord> list;
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db, "SELECT friend_id,is_blocked FROM Friends WHERE user_id=?;", -1, &st, nullptr);
    sqlite3_bind_int(st,1,userId);
    while (sqlite3_step(st)==SQLITE_ROW) {
        FriendRecord fr;
        fr.friendId = sqlite3_column_int(st,0);
        fr.isBlocked = sqlite3_column_int(st,1);
        list.push_back(fr);
    }
    sqlite3_finalize(st);
    return list;
}

bool DatabaseManager::getPendingFriendRequests(int userId, std::vector<std::pair<int, int>>& requests) {
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "SELECT request_id, user_id FROM FriendRequests WHERE friend_id = ? AND status = 0;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, userId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int requestId = sqlite3_column_int(stmt, 0);
        int fromUserId = sqlite3_column_int(stmt, 1);
        requests.emplace_back(requestId, fromUserId);
    }

    sqlite3_finalize(stmt);
    return true;
}

std::vector<MessageRecord> DatabaseManager::loadOffline(int receiverId, int groupId) {
    std::vector<MessageRecord> msgs;
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *st;
    
    if (groupId == -1) {
        // 查询私聊消息
        sqlite3_prepare_v2(db, "SELECT msg_id, sender_id, receiver_id, content FROM Messages WHERE receiver_id=? AND delivered=0;", -1, &st, nullptr);
        sqlite3_bind_int(st, 1, receiverId);
    } else {
        // 查询群组消息
        sqlite3_prepare_v2(db, "SELECT msg_id, sender_id, receiver_id, content FROM Messages WHERE group_id=? AND delivered=0;", -1, &st, nullptr);
        sqlite3_bind_int(st, 1, groupId);
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        MessageRecord rec;
        rec.msgId = sqlite3_column_int(st, 0);
        rec.sender = sqlite3_column_int(st, 1);
        rec.receiver = sqlite3_column_int(st, 2);
        rec.content = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        msgs.push_back(rec);
    }
    sqlite3_finalize(st);
    return msgs;
}

bool DatabaseManager::markDelivered(int msgId) {
    std::lock_guard<std::mutex> l(mtx);
    return executePrepared("UPDATE Messages SET delivered=1 WHERE msg_id=?;", {
        {std::to_string(msgId), SQLITE_INTEGER}
    });
}

std::string sha256(const std::string &input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return oss.str();
}

bool DatabaseManager::executePrepared(const std::string &sql, const std::vector<std::pair<std::string, int>> &params) {
    std::cout << "[DEBUG] Preparing SQL: " << sql << std::endl;  // 新增
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ERROR] Prepare failed: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    for (size_t i = 0; i < params.size(); ++i) {
        const auto &p = params[i];
        std::cout << "[DEBUG] Param " << (i+1) << ": " << p.first << std::endl;  // 新增
        if (p.second == SQLITE_INTEGER)
            sqlite3_bind_int(stmt, i + 1, std::stoi(p.first));
        else
            sqlite3_bind_text(stmt, i + 1, p.first.c_str(), -1, SQLITE_TRANSIENT);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    std::cout << "[DEBUG] Step result: " << rc << std::endl;  // 新增
    return rc == SQLITE_DONE;
}

bool DatabaseManager::storeFileTransfer(int senderId, int receiverId, const std::string &fileName, const std::vector<uint8_t>& fileData) {
    std::lock_guard<std::mutex> l(mtx);
    return executePrepared("INSERT INTO FileTransfers(sender_id, receiver_id, file_name, file_data, status) VALUES(?, ?, ?, ?, 0);", {
        {std::to_string(senderId), SQLITE_INTEGER},
        {std::to_string(receiverId), SQLITE_INTEGER},
        {fileName, SQLITE_TEXT},
        {std::string(fileData.begin(), fileData.end()), SQLITE_BLOB}
    });
}

std::vector<FileTransferRecord> DatabaseManager::getFileTransfers(int receiverId) {
    std::vector<FileTransferRecord> files;
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "SELECT file_id, sender_id, receiver_id, file_name, file_data FROM FileTransfers WHERE receiver_id=? AND status=0;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return files;
    }

    sqlite3_bind_int(stmt, 1, receiverId);  // 群组ID

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileTransferRecord record;
        record.fileId = sqlite3_column_int(stmt, 0);
        record.senderId = sqlite3_column_int(stmt, 1);
        record.receiverId = sqlite3_column_int(stmt, 2);
        record.fileName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const uint8_t* fileBlob = reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, 4));
        int fileSize = sqlite3_column_bytes(stmt, 4);
        record.fileData = std::vector<uint8_t>(fileBlob, fileBlob + fileSize);
        files.push_back(record);
    }

    sqlite3_finalize(stmt);
    return files;
}

// 创建群组
bool DatabaseManager::createGroup(const std::string &groupName) {
    std::lock_guard<std::mutex> l(mtx);
    int groupId = getGroupIdByName(groupName);
    if (groupId != -1) {
        std::cerr << "[ERROR] 群组 " << groupName << " 已经存在！" << std::endl;
        return false;
    }
    return executePrepared("INSERT INTO Groups(group_name) VALUES(?);", {
        {groupName, SQLITE_TEXT}
    });
}

// 获取群组ID
int DatabaseManager::getGroupIdByName(const std::string &groupName) {
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db, "SELECT group_id FROM Groups WHERE group_name=?;", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, groupName.c_str(), -1, SQLITE_TRANSIENT);
    int groupId = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        groupId = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return groupId;
}
// 加入群组
bool DatabaseManager::addUserToGroup(int userId, const std::string &groupName) {
    std::lock_guard<std::mutex> l(mtx);
    int groupId = getGroupIdByName(groupName);
    if (groupId == -1) {
        std::cerr << "[ERROR] 群组 " << groupName << " 不存在" << std::endl;
        return false;
    }
    // 将用户添加到群组
    return executePrepared("INSERT INTO GroupMembers(group_id, user_id) VALUES(?, ?);", {
        {std::to_string(groupId), SQLITE_INTEGER}, {std::to_string(userId), SQLITE_INTEGER}
    });
}

bool DatabaseManager::isUserInGroup(int userId, int groupId) {
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "SELECT COUNT(*) FROM GroupMembers WHERE group_id = ? AND user_id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, groupId);  // 群组ID
    sqlite3_bind_int(stmt, 2, userId);   // 用户ID

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count > 0;  // 如果查询结果大于0，表示用户已是群组成员
}

std::vector<int> DatabaseManager::getGroupMembers(int groupId) {
    std::vector<int> members;
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *stmt = nullptr;

    const char *sql = "SELECT user_id FROM GroupMembers WHERE group_id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return members;
    }

    sqlite3_bind_int(stmt, 1, groupId);  // 群组ID

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int userId = sqlite3_column_int(stmt, 0);
        members.push_back(userId);  // 将成员ID添加到列表中
    }

    sqlite3_finalize(stmt);
    return members;
}

bool DatabaseManager::storeMessage(int senderId, int receiverId, const std::string &content, int groupId) {
    std::lock_guard<std::mutex> l(mtx);
    if (groupId == -1) {
        // 私聊消息
        return executePrepared("INSERT INTO Messages(sender_id, receiver_id, content, delivered) VALUES(?, ?, ?, 0);", {
            {std::to_string(senderId), SQLITE_INTEGER},
            {std::to_string(receiverId), SQLITE_INTEGER},
            {content, SQLITE_TEXT}
        });
    } else {
        // 群组消息
        return executePrepared("INSERT INTO Messages(sender_id, receiver_id, group_id, content, delivered) VALUES(?, ?, ?, ?, 0);", {
            {std::to_string(senderId), SQLITE_INTEGER},
            {std::to_string(receiverId), SQLITE_INTEGER},  // receiverId 可以是群组代表或创建者
            {std::to_string(groupId), SQLITE_INTEGER},
            {content, SQLITE_TEXT}
        });
    }
}

std::vector<MessageRecord> DatabaseManager::getGroupMessages(int groupId) {
    std::vector<MessageRecord> msgs;
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db, "SELECT msg_id, sender_id, content FROM Messages WHERE group_id=?;", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, groupId);

    while (sqlite3_step(st) == SQLITE_ROW) {
        MessageRecord rec;
        rec.msgId = sqlite3_column_int(st, 0);
        rec.sender = sqlite3_column_int(st, 1);
        rec.content = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        msgs.push_back(rec);
    }
    sqlite3_finalize(st);
    return msgs;
}

std::vector<MessageRecord> DatabaseManager::getPrivateMessages(int userId) {
    std::vector<MessageRecord> msgs;
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db, "SELECT msg_id, sender_id, receiver_id, content FROM Messages WHERE (sender_id=? OR receiver_id=?) AND delivered=0;", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, userId);
    sqlite3_bind_int(st, 2, userId);

    while (sqlite3_step(st) == SQLITE_ROW) {
        MessageRecord rec;
        rec.msgId = sqlite3_column_int(st, 0);
        rec.sender = sqlite3_column_int(st, 1);
        rec.receiver = sqlite3_column_int(st, 2);
        rec.content = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        msgs.push_back(rec);
    }
    sqlite3_finalize(st);
    return msgs;
}

void DatabaseManager::sendGroupMessage(int senderId, const std::string &content, int groupId) {
    // 获取群组成员
    std::vector<int> members = getGroupMembers(groupId);

    // 将消息存入数据库，并发送给每个成员
    for (int memberId : members) {
        if (memberId != senderId) {
            storeMessage(senderId, memberId, content, groupId);  // 为每个成员存储群组消息
        }
    }
}

std::vector<MessageRecord> DatabaseManager::getChatHistory(int userId, int friendId, int limit) {
    std::vector<MessageRecord> msgs;
    std::lock_guard<std::mutex> l(mtx);
    sqlite3_stmt *st;

    const char *sql =
        "SELECT msg_id, sender_id, receiver_id, content, timestamp "
        "FROM Messages "
        "WHERE group_id IS NULL AND "
        "((sender_id = ? AND receiver_id = ?) OR (sender_id = ? AND receiver_id = ?)) "
        "ORDER BY timestamp DESC LIMIT ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        std::cerr << "[ERROR] prepare getChatHistory: " << sqlite3_errmsg(db) << std::endl;
        return msgs;
    }

    sqlite3_bind_int(st, 1, userId);
    sqlite3_bind_int(st, 2, friendId);
    sqlite3_bind_int(st, 3, friendId);
    sqlite3_bind_int(st, 4, userId);
    sqlite3_bind_int(st, 5, limit);

    while (sqlite3_step(st) == SQLITE_ROW) {
        MessageRecord rec;
        rec.msgId = sqlite3_column_int(st, 0);
        rec.sender = sqlite3_column_int(st, 1);
        rec.receiver = sqlite3_column_int(st, 2);
        rec.content = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        rec.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));  // 如果你有 timestamp 字段

        msgs.push_back(rec);
    }

    sqlite3_finalize(st);
    return msgs;
}
