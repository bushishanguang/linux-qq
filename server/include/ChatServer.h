#ifndef CHATSERVER_H
#define CHATSERVER_H

#include "ThreadPool.h"
#include "DatabaseManager.h"
#include "Protocol.h"
#include <netinet/in.h>
#include <unordered_map>
#include <mutex>
#include <vector>

struct ClientInfo {
    int userId;
    sockaddr_in addr;
};

class ChatServer {
public:
    ChatServer(int port, const std::string &dbFile);
    ~ChatServer();

    bool init();
    bool start();

private:
    void receiveLoop();
    void handlePacket(const sockaddr_in &addr, const std::vector<uint8_t> &data);

    // 账户相关
    void handleRegister(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleLogin(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleLogout(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleUpdateUser(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleDeleteUser(const sockaddr_in &addr, const std::vector<uint8_t> &body);

    // 好友相关
    void handleFriendRequest(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleFriendRequestList(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleFriendRequestAction(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleDeleteFriend(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleFriendList(const sockaddr_in &addr, const std::vector<uint8_t> &body);

    // 黑名单相关
    void handleBlockUser(const sockaddr_in &addr, const std::vector<uint8_t> &body);
    void handleUnblockUser(const sockaddr_in &addr, const std::vector<uint8_t> &body);

    // 群组相关
    void handleCreateGroup(const sockaddr_in &addr, const std::vector<uint8_t> &body);  // 创建群组
    void handleJoinGroup(const sockaddr_in &addr, const std::vector<uint8_t> &body);    // 加入群组
    void sendGroupMessage(const sockaddr_in &addr, int groupId, const std::string &message);  // 发送群组消息

    // 私聊相关
    void handlePrivateMessage(const sockaddr_in &addr, const std::vector<uint8_t> &body); // 处理私聊消息

    int sockfd;
    sockaddr_in serverAddr;
    ThreadPool pool;
    DatabaseManager db;

    std::mutex clientsMutex;
    std::unordered_map<int, ClientInfo> onlineClients;
};

#endif // CHATSERVER_H
