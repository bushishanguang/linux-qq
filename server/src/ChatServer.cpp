#include "ChatServer.h"
#include "Config.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

namespace {
void sendPacket(int sockfd, const sockaddr_in &addr, MessageType type, const std::vector<uint8_t> &payload) {
    PacketHeader hdr{ type, static_cast<uint32_t>(payload.size()) };
    std::vector<uint8_t> packet(sizeof(hdr) + payload.size());
    memcpy(packet.data(), &hdr, sizeof(hdr));
    memcpy(packet.data() + sizeof(hdr), payload.data(), payload.size());
    sendto(sockfd, packet.data(), packet.size(), 0,
           reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    std::cout << "[SEND] Type: " << static_cast<int>(type) << ", Payload: " << payload.size() << std::endl;
}

void sendSimpleResponseWithLog(int sockfd, const sockaddr_in &addr, MessageType type, bool ok, const std::string &msg) {
    PacketHeader resp{ type, 1 };
    uint8_t buf[sizeof(resp) + 1];
    memcpy(buf, &resp, sizeof(resp));
    buf[sizeof(resp)] = ok ? 1 : 0;
    sendto(sockfd, buf, sizeof(buf), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    std::cout << "[RESP] " << msg << (ok ? " Success" : " Fail") << std::endl;
}
}

ChatServer::ChatServer(int port, const std::string &dbFile)
    : sockfd(-1), serverAddr{}, pool(4), db(dbFile) {
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
}

ChatServer::~ChatServer() {
    if (sockfd >= 0) close(sockfd);
    pool.shutdown();
}

bool ChatServer::init() {
    return db.init();
}

bool ChatServer::start() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return false; }
    if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind"); close(sockfd); return false;
    }
    std::cout << "[INFO] Server listening on port " << ntohs(serverAddr.sin_port) << std::endl;
    receiveLoop();
    return true;
}

void ChatServer::receiveLoop() {
    while (true) {
        sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        uint8_t buf[2048];
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0,
                             (struct sockaddr*)&clientAddr, &len);
        if (n <= static_cast<ssize_t>(sizeof(PacketHeader))) continue;
        std::vector<uint8_t> data(buf, buf + n);
        pool.enqueue([this, clientAddr, data](){ handlePacket(clientAddr, data); });
    }
}

void ChatServer::handlePacket(const sockaddr_in &addr, const std::vector<uint8_t> &data) {
    PacketHeader hdr;
    memcpy(&hdr, data.data(), sizeof(hdr));
    std::vector<uint8_t> body(data.begin() + sizeof(hdr), data.end());
    std::cout << "[RECV] Packet type: " << static_cast<int>(hdr.type)
              << ", from: " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
    switch (hdr.type) {
        case REGISTER_REQ:               handleRegister(addr, body);                break;
        case LOGIN_REQ:                  handleLogin(addr, body);                   break;
        case LOGOUT_REQ:                 handleLogout(addr, body);                  break;
        case CREATE_GROUP_REQ:           handleCreateGroup(addr, body);             break;  // 创建群组请求
        case JOIN_GROUP_REQ:             handleJoinGroup(addr, body);               break;  // 加入群组请求
        case PRIVATE_MSG_REQ:            handlePrivateMessage(addr, body);          break;
        case FRIEND_REQUEST_REQ:         handleFriendRequest(addr, body);           break;
        case FRIEND_REQUEST_LIST_REQ:    handleFriendRequestList(addr, body);       break;
        case FRIEND_REQUEST_ACTION_REQ:  handleFriendRequestAction(addr, body);     break;
        case DELETE_FRIEND_REQ:          handleDeleteFriend(addr, body);            break;
        case FRIEND_LIST_REQ:            handleFriendList(addr, body);              break;
        case BLOCK_USER_REQ:             handleBlockUser(addr, body);               break;
        case UNBLOCK_USER_REQ:           handleUnblockUser(addr, body);             break;
        default:
            std::cerr << "[WARN] Unknown packet type: " << static_cast<int>(hdr.type) << std::endl;
            break;
    }
}


void ChatServer::handleRegister(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    const char *p = reinterpret_cast<const char*>(body.data());
    bool ok = db.registerUser(p, p + strlen(p) + 1);
    sendSimpleResponseWithLog(sockfd, addr, REGISTER_RESP, ok, "Register");
}

void ChatServer::handleLogin(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    const char *p = reinterpret_cast<const char*>(body.data());
    int userId = -1;
    bool ok = db.verifyUser(p, p + strlen(p) + 1, userId);  // 验证用户

    std::cout << "[DEBUG] Login attempt by userId: " << userId << std::endl;

    // 检查用户是否已在线
    if (ok) {
        std::lock_guard<std::mutex> lk(clientsMutex);

        if (onlineClients.count(userId)) {  // 用户已在线，登录失败
            ok = false;
            std::cout << "[INFO] 用户 " << userId << " 已在线，无法重新登录" << std::endl;
        } else {
            onlineClients[userId] = ClientInfo{userId, addr};
            std::cout << "[INFO] 用户 " << userId << " 成功登录" << std::endl;
        }
    }

    // 准备登录响应
    std::vector<uint8_t> payload(1 + sizeof(int));
    payload[0] = ok;
    int netUserId = htonl(userId);
    memcpy(payload.data() + 1, &netUserId, sizeof(int));
    sendPacket(sockfd, addr, LOGIN_RESP, payload);  // 发送登录响应
    std::cout << "[RESP] Login " << (ok ? "Success" : "Fail") << std::endl;
}


void ChatServer::handleLogout(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId;
    memcpy(&userId, body.data(), sizeof(userId));

    // 将用户 ID 从网络字节序转换为主机字节序
    userId = ntohl(userId);

    // 确保用户从在线用户列表中移除
    {
        std::lock_guard<std::mutex> lk(clientsMutex);
        if (onlineClients.count(userId)) {
            onlineClients.erase(userId);  // 清理在线状态
            std::cout << "[INFO] 用户 " << userId << " 已成功退出登录" << std::endl;
        } else {
            std::cout << "[INFO] 用户 " << userId << " 不在在线状态" << std::endl;
        }
    }

    // 响应客户端，确认退出
    sendSimpleResponseWithLog(sockfd, addr, LOGOUT_RESP, true, "Logout");
}




void ChatServer::handleUpdateUser(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId; memcpy(&userId, body.data(), sizeof(userId));
    const char *p = reinterpret_cast<const char*>(body.data() + sizeof(userId));
    bool ok = db.updateUser(userId, p, p + strlen(p) + 1);
    sendSimpleResponseWithLog(sockfd, addr, UPDATE_USER_RESP, ok, "UpdateUser");
}

void ChatServer::handleDeleteUser(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId; memcpy(&userId, body.data(), sizeof(userId));
    bool ok = db.deleteUser(userId);
    sendSimpleResponseWithLog(sockfd, addr, DELETE_USER_RESP, ok, "DeleteUser");
    if (ok) {
        std::lock_guard<std::mutex> lk(clientsMutex);
        onlineClients.erase(userId);
    }
}

void ChatServer::handleFriendRequest(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int u, f;
    memcpy(&u, body.data(), sizeof(u));
    memcpy(&f, body.data() + sizeof(u), sizeof(f));

    if (u == f) {
        std::cerr << "[ERROR] 用户尝试添加自己为好友" << std::endl;
        sendSimpleResponseWithLog(sockfd, addr, FRIEND_REQUEST_RESP, false, "FriendRequest - 用户尝试添加自己");
        return;
    }

    // 检查是否已经发送过好友请求（包括双向请求）
    bool alreadyRequested = db.isFriendRequestExists(u, f) || db.isFriendRequestExists(f, u);
    if (alreadyRequested) {
        std::cout << "[INFO] 用户 " << u << " 和用户 " << f << " 之间已经有待确认的好友请求" << std::endl;
        sendSimpleResponseWithLog(sockfd, addr, FRIEND_REQUEST_RESP, false, "FriendRequest - 已有待确认请求");
        return;
    }

    // 如果没有重复请求，则继续发送好友请求
    bool ok = db.sendFriendRequest(u, f);
    sendSimpleResponseWithLog(sockfd, addr, FRIEND_REQUEST_RESP, ok, "FriendRequest");
}


void ChatServer::handleFriendRequestAction(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int requestId;
    memcpy(&requestId, body.data(), sizeof(requestId));
    bool accept = body[sizeof(requestId)] != 0;

    std::cout << "[DEBUG] handleFriendRequestAction called, id=" << requestId
              << ", accept=" << accept << std::endl;

    bool ok = db.respondFriendRequest(requestId, accept);

    std::cout << "[DEBUG] respondFriendRequest returned: " << ok << std::endl;

    sendSimpleResponseWithLog(sockfd, addr, FRIEND_REQUEST_ACTION_RESP, ok, "FriendRequestAction");
}


void ChatServer::handleDeleteFriend(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId, friendId;
    memcpy(&userId, body.data(), sizeof(userId));
    memcpy(&friendId, body.data() + sizeof(userId), sizeof(friendId));
    bool ok = db.deleteFriend(userId, friendId);
    sendSimpleResponseWithLog(sockfd, addr, DELETE_FRIEND_RESP, ok, "DeleteFriend");
}

void ChatServer::handleFriendList(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId;
    memcpy(&userId, body.data(), sizeof(userId));

    auto friends = db.getFriends(userId);

    std::vector<uint8_t> payload;
    payload.push_back(1);  // 成功标志

    for (const auto &f : friends) {
        // 将好友ID转换为网络字节序并插入到 payload
        int netId = htonl(f.friendId);
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&netId), reinterpret_cast<uint8_t*>(&netId) + sizeof(int));

        // 将 isBlocked 状态添加为 1 字节
        payload.push_back(f.isBlocked ? 1 : 0);
    }

    // 发送响应包
    sendPacket(sockfd, addr, FRIEND_LIST_RESP, payload);
    std::cout << "[RESP] FriendList, count = " << friends.size() << std::endl;
}

void ChatServer::handleFriendRequestList(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId; memcpy(&userId, body.data(), sizeof(userId));
    auto requests = db.getFriendRequests(userId);

    std::vector<uint8_t> payload;
    payload.push_back(1);

    for (auto &r : requests) {
        int netReqId = htonl(r.requestId);
        int netUserId = htonl(r.userId);
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&netReqId), reinterpret_cast<uint8_t*>(&netReqId) + sizeof(int));
        payload.insert(payload.end(), reinterpret_cast<uint8_t*>(&netUserId), reinterpret_cast<uint8_t*>(&netUserId) + sizeof(int));
    }

    sendPacket(sockfd, addr, FRIEND_REQUEST_LIST_RESP, payload);
    std::cout << "[RESP] FriendRequestList Success, count = " << requests.size() << std::endl;
}


void ChatServer::handleBlockUser(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId, targetId;
    memcpy(&userId, body.data(), sizeof(userId));
    memcpy(&targetId, body.data() + sizeof(userId), sizeof(targetId));
    bool ok = db.blockFriend(userId, targetId);
    sendSimpleResponseWithLog(sockfd, addr, BLOCK_USER_RESP, ok, "BlockUser");
}

void ChatServer::handleUnblockUser(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId, targetId;
    memcpy(&userId, body.data(), sizeof(userId));
    memcpy(&targetId, body.data() + sizeof(userId), sizeof(targetId));
    bool ok = db.unblockFriend(userId, targetId);
    sendSimpleResponseWithLog(sockfd, addr, UNBLOCK_USER_RESP, ok, "UnblockUser");
}

void ChatServer::handleCreateGroup(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    const char *groupName = reinterpret_cast<const char*>(body.data());  // 提取群组名称
    // 检查群组是否已存在
    int groupId = db.getGroupIdByName(groupName);
    if (groupId != -1) {
        sendSimpleResponseWithLog(sockfd, addr, CREATE_GROUP_RESP, false, "Group already exists");
        return;
    }

    bool ok = db.createGroup(groupName);  // 调用数据库函数创建群组

    sendSimpleResponseWithLog(sockfd, addr, CREATE_GROUP_RESP, ok, ok ? "CreateGroup" : "CreateGroup - Error");
}


void ChatServer::handleJoinGroup(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int userId;
    memcpy(&userId, body.data(), sizeof(userId));  // 提取用户ID
    std::string groupName(reinterpret_cast<const char*>(body.data() + sizeof(userId)));  // 提取群组名称

    // 检查用户是否已经是群组成员
    int groupId = db.getGroupIdByName(groupName);
    if (groupId == -1) {
        sendSimpleResponseWithLog(sockfd, addr, JOIN_GROUP_RESP, false, "Group does not exist");
        return;
    }

    bool isAlreadyMember = db.isUserInGroup(userId, groupId);  // 判断用户是否已是群组成员
    if (isAlreadyMember) {
        sendSimpleResponseWithLog(sockfd, addr, JOIN_GROUP_RESP, false, "Already a member of this group");
        return;
    }

    bool ok = db.addUserToGroup(userId, groupName);  // 调用数据库函数将用户加入群组

    sendSimpleResponseWithLog(sockfd, addr, JOIN_GROUP_RESP, ok, ok ? "JoinGroup" : "JoinGroup - Error");
}


void ChatServer::sendGroupMessage(const sockaddr_in &addr, int groupId, const std::string &message) {
    // 获取群组成员
    std::vector<int> groupMembers = db.getGroupMembers(groupId);

    for (int memberId : groupMembers) {
        if (onlineClients.count(memberId)) {
            // 发送消息给在线用户
            sendPacket(sockfd, onlineClients[memberId].addr, GROUP_MSG, {message.begin(), message.end()});
        } else {
            // 存储离线消息，待用户上线后再发送
            db.storeMessage(0, memberId, message);  // 0表示群组消息的发送者
        }
    }
}

void ChatServer::handlePrivateMessage(const sockaddr_in &addr, const std::vector<uint8_t> &body) {
    int senderId, receiverId;
    memcpy(&senderId, body.data(), sizeof(senderId));
    memcpy(&receiverId, body.data() + sizeof(senderId), sizeof(receiverId));
    std::string message(reinterpret_cast<const char*>(body.data() + 2 * sizeof(int)));

    std::cout << "[DEBUG] Handling private message from " << senderId << " to " << receiverId << std::endl;

    // 检查用户是否为好友关系
    auto friends = db.getFriends(senderId);
    bool isFriend = false;
    for (const auto& fr : friends) {
        if (fr.friendId == receiverId && !fr.isBlocked) {
            isFriend = true;
            break;
        }
    }

    if (!isFriend) {
        std::cerr << "[ERROR] Users are not friends or are blocked" << std::endl;
        sendSimpleResponseWithLog(sockfd, addr, PRIVATE_MSG_RESP, false, "Not friends or blocked");
        return;
    }

    // 检查接收者是否在线
    {
        std::lock_guard<std::mutex> lk(clientsMutex);
        if (onlineClients.count(receiverId)) {
            // 如果在线，直接发送消息
            sendPacket(sockfd, onlineClients[receiverId].addr, PRIVATE_MSG_RESP, {message.begin(), message.end()});
        } else {
            // 如果离线，存储离线消息
            db.storeMessage(senderId, receiverId, message);  // senderId -> receiverId 的私聊消息
        }
    }

    sendSimpleResponseWithLog(sockfd, addr, PRIVATE_MSG_RESP, true, "PrivateMessage");
}


