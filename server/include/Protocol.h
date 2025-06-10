#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <string>

enum MessageType : uint8_t {
    LOGIN_REQ = 1,
    LOGIN_RESP,
    REGISTER_REQ,
    REGISTER_RESP,
    UPDATE_USER_REQ,
    UPDATE_USER_RESP,
    DELETE_USER_REQ,
    DELETE_USER_RESP,
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

    // === 聊天、退出、新功能 ===
    CHAT_MSG_REQ = 100,
    CHAT_MSG_RESP = 101,
    OFFLINE_MSG_LIST_RESP = 102,
    LOGOUT_REQ = 103,
    LOGOUT_RESP = 104,

    // === 群聊、文件、心跳 ===
    GROUP_MSG = 120,              // 群组消息
    FILE_REQ,
    FILE_ACK,
    FILE_DATA,
    FILE_DONE,
    HEARTBEAT,

    // === 群组相关 ===
    CREATE_GROUP_REQ = 130,       // 创建群组请求
    CREATE_GROUP_RESP,            // 创建群组响应
    JOIN_GROUP_REQ,               // 加入群组请求
    JOIN_GROUP_RESP,              // 加入群组响应

    // === 好友私聊 ===
    PRIVATE_MSG_REQ = 140,        // 私聊消息请求
    PRIVATE_MSG_RESP              // 私聊消息响应
};

// 协议头结构
struct PacketHeader {
    uint8_t  type;        // 消息类型
    uint32_t length;      // 消息长度
};

// 私聊消息请求结构
struct PrivateMsgReq {
    int senderId;         // 发送者ID
    int receiverId;       // 接收者ID
    std::string message;  // 消息内容
};

// 私聊消息响应结构
struct PrivateMsgResp {
    bool success;         // 成功标志
    std::string errorMsg; // 错误消息（如果有）
};

#endif // PROTOCOL_H
