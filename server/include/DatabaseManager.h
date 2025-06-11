#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>

// 好友记录结构体
struct FriendRecord {
    int friendId;
    bool isBlocked;
};

// 好友请求记录结构体
struct FriendRequestRecord {
    int requestId;
    int userId;
    int friendId;
    int status;
};

// 消息记录结构体
struct MessageRecord {
    int msgId;
    int sender;
    int receiver;
    std::string content;
    bool delivered;
    std::string timestamp;
};

// 群组记录结构体
struct GroupRecord {
    int groupId;
    std::string groupName;
};

// 群组成员记录结构体
struct GroupMemberRecord {
    int groupId;
    int userId;
};

// 文件传输记录结构体
struct FileTransferRecord {
    int fileId;
    int senderId;
    int receiverId;
    std::string fileName;
    std::vector<uint8_t> fileData;
};

class DatabaseManager {
public:
    // 构造函数和析构函数
    DatabaseManager(const std::string &dbFile);
    ~DatabaseManager();

    // 数据库初始化
    bool init();

    // 用户注册与验证
    bool registerUser(const std::string &u, const std::string &p);
    bool verifyUser(const std::string &u, const std::string &p, int &userId);
    bool updateUser(int userId, const std::string &newName, const std::string &newPwd);
    bool deleteUser(int userId);

    // 执行SQL语句（准备好参数）
    bool executePrepared(const std::string &sql, const std::vector<std::pair<std::string, int>> &params);

    // 好友请求与管理
    bool isFriendRequestExists(int userId, int friendId);
    bool sendFriendRequest(int userId, int friendId);
    std::vector<FriendRequestRecord> getFriendRequests(int userId);
    bool respondFriendRequest(int requestId, bool accept);
    bool addFriend(int userId, int friendId);
    bool deleteFriend(int userId, int friendId);
    bool blockFriend(int userId, int friendId);
    bool unblockFriend(int userId, int friendId);
    std::vector<FriendRecord> getFriends(int userId);
    bool getPendingFriendRequests(int userId, std::vector<std::pair<int, int>>& requests);

    // 消息管理
    bool storeMessage(int senderId, int receiverId, const std::string &content, int groupId = -1);
    std::vector<MessageRecord> loadOffline(int receiverId, int groupId = -1);
    bool markDelivered(int msgId);
    std::vector<MessageRecord> getChatHistory(int userId, int friendId, int limit = 50);

    // 群组管理
    bool createGroup(const std::string &groupName);  // 创建群组
    bool addUserToGroup(int userId, const std::string &groupName);  // 用户加入群组
    int getGroupIdByName(const std::string &groupName);  // 根据群组名获取群组ID
    bool isUserInGroup(int userId, int groupId);   // 检查用户是否已是群组成员
    std::vector<int> getGroupMembers(int groupId); // 获取群组成员列表

    //群组消息管理
    std::vector<MessageRecord> getGroupMessages(int groupId);
    std::vector<MessageRecord> getPrivateMessages(int userId);
    void sendGroupMessage(int senderId, const std::string &content, int groupId);

    // 文件传输管理
    bool storeFileTransfer(int senderId, int receiverId, const std::string &fileName, const std::vector<uint8_t>& fileData);
    std::vector<FileTransferRecord> getFileTransfers(int receiverId);

private:
    sqlite3 *db;  // SQLite数据库指针
    std::mutex mtx;  // 互斥锁用于线程同步

    // 执行SQL语句（无参数）
    bool execute(const std::string &sql);
};

#endif // DATABASEMANAGER_H
