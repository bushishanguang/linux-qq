#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

// === 消息类型定义 ===
enum MessageType : uint8_t {
    // === 账户相关 ===
    REGISTER_REQ = 1,
    REGISTER_RESP,
    LOGIN_REQ,
    LOGIN_RESP,
    LOGOUT_REQ,
    LOGOUT_RESP,
    UPDATE_USER_REQ,
    UPDATE_USER_RESP,
    DELETE_USER_REQ,
    DELETE_USER_RESP,

    // === 好友系统 ===
    FRIEND_REQUEST_REQ,
    FRIEND_REQUEST_RESP,
    FRIEND_REQUEST_LIST_REQ,
    FRIEND_REQUEST_LIST_RESP,
    FRIEND_REQUEST_ACTION_REQ,
    FRIEND_REQUEST_ACTION_RESP,
    DELETE_FRIEND_REQ,
    DELETE_FRIEND_RESP,
    BLOCK_USER_REQ,
    BLOCK_USER_RESP,
    UNBLOCK_USER_REQ,
    UNBLOCK_USER_RESP,
    FRIEND_LIST_REQ,
    FRIEND_LIST_RESP,

    // === 群组系统 ===
    CREATE_GROUP_REQ,
    CREATE_GROUP_RESP,
    JOIN_GROUP_REQ,
    JOIN_GROUP_RESP,
    GROUP_MSG,

    // === 私聊系统 ===
    PRIVATE_MSG_REQ = 140,
    PRIVATE_MSG_RESP,
    PRIVATE_MSG_PUSH,
    OFFLINE_MSG_LIST_RESP,

    // === 聊天记录 ===
    CHAT_HISTORY_REQ,
    CHAT_HISTORY_RESP
};

// === 协议头结构 ===
struct PacketHeader {
    uint8_t type;
    uint32_t length;  // payload长度
};

// === 私聊消息 ===
struct PrivateMsgReq {
    int senderId;
    int receiverId;
    std::string message;
};

struct PrivateMsgResp {
    bool success;
    std::string errorMsg;
};

struct PrivateMsgPush {
    int senderId;
    std::string message;
    std::string timestamp;
};

// === 离线消息结构 ===
struct OfflineMsg {
    int msgId;
    int senderId;
    std::string content;
    std::string timestamp;
};

struct OfflineMsgListResp {
    std::vector<OfflineMsg> messages;
};

// === 聊天记录请求与响应 ===
struct ChatHistoryReq {
    int userId;
    int friendId;
    int limit;
};

struct ChatMessage {
    int senderId;
    std::string content;
    std::string timestamp;
};

struct ChatHistoryResp {
    std::vector<ChatMessage> messages;
};

#endif // PROTOCOL_H
