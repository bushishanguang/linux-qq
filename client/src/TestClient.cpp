#include "Protocol.h"
#include "Config.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <limits>
#include <cstring>
#include <functional>
#include <csignal> 

int currentUserId = -1;
int sock;
sockaddr_in serv;

std::vector<uint8_t> buildPacket(MessageType type, const std::vector<uint8_t>& body) {
    PacketHeader hdr{type, static_cast<uint32_t>(body.size())};
    std::vector<uint8_t> pkt(sizeof(hdr) + body.size());
    memcpy(pkt.data(), &hdr, sizeof(hdr));
    if (!body.empty())
        memcpy(pkt.data() + sizeof(hdr), body.data(), body.size());
    return pkt;
}

void sendRequest(const std::vector<uint8_t> &pkt) {
    // 发送数据包
    if (sendto(sock, pkt.data(), pkt.size(), 0, reinterpret_cast<const sockaddr*>(&serv), sizeof(serv)) < 0) {
        perror("sendto");
        return;
    }
    std::cout << "[DEBUG] Sent request packet of size: " << pkt.size() << std::endl;
    
    uint8_t buf[2048];
    socklen_t len = sizeof(serv);
    ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&serv), &len);

    if (n <= static_cast<ssize_t>(sizeof(PacketHeader))) {
        std::cerr << "无效响应或者超时" << std::endl;
        return;
    }

    PacketHeader r;
    memcpy(&r, buf, sizeof(r));
    r.length = ntohl(r.length);
    bool ok = buf[sizeof(r)];
    std::cout << "响应类型：" << static_cast<int>(r.type)
              << (ok ? " 成功" : " 失败") << std::endl;

    switch (r.type) {
        case LOGIN_RESP: {
            bool loginOk = buf[sizeof(r)];
            if (loginOk) {
                memcpy(&currentUserId, buf + sizeof(r) + 1, sizeof(currentUserId));
                currentUserId = ntohl(currentUserId);
                std::cout << ", 登录用户ID = " << currentUserId << std::endl;
            } else {
                std::cout << ", 登录失败，用户可能已经在线或用户名/密码错误" << std::endl;
            }
            break;
        }

        case LOGOUT_RESP:
            if (ok) {
                currentUserId = -1;
                std::cout << ", 已退出登录" << std::endl;
            }   
            break;

        case DELETE_USER_RESP:
            if (ok) {
                currentUserId = -1;
                std::cout << ", 当前账户已注销，自动退出登录" << std::endl;
            }
            break;

        case FRIEND_REQUEST_LIST_RESP: {
            r.length = ntohl(r.length);
            bool listOk = buf[sizeof(r)] != 0;
            if (!listOk) {
                std::cout << ", 查看好友请求失败" << std::endl;
                return;
            }

            const uint8_t* p = buf + sizeof(r) + 1;
            size_t remaining = n - (sizeof(r) + 1);
            if (remaining % (sizeof(int) * 2) != 0) {
                std::cerr << "响应格式错误（长度不匹配）" << std::endl;
                return;
            }

            int count = remaining / (sizeof(int) * 2);
            std::cout << "\n[好友请求列表] 共 " << count << " 条请求：" << std::endl;

            for (int i = 0; i < count; ++i) {
                int reqId, fromId;
                memcpy(&reqId, p, sizeof(int)); p += sizeof(int);
                memcpy(&fromId, p, sizeof(int)); p += sizeof(int);
                reqId = ntohl(reqId);
                fromId = ntohl(fromId);
                std::cout << "请求ID: " << reqId << ", 来自用户ID: " << fromId << std::endl;
            }
            break;
        }

        case FRIEND_LIST_RESP: {
            size_t hdrSize = sizeof(r);
            const uint8_t* p = buf + hdrSize + 1;
            size_t remaining = n - (hdrSize + 1);

            std::cout << "\n[DEBUG] 好友列表总响应长度: " << n
                    << ", header: " << hdrSize << ", body: " << remaining << std::endl;

            if (remaining % 5 != 0) {
                std::cerr << "[错误] 好友列表响应格式不正确（剩余: " << remaining << "）" << std::endl;
                return;
            }

            int count = remaining / 5;
            std::cout << "[好友列表] 共 " << count << " 人：" << std::endl;

            for (int i = 0; i < count; ++i) {
                uint32_t fid = 0;
                memcpy(&fid, p, 4); fid = ntohl(fid); p += 4;
                uint8_t blocked = *p++;
                std::cout << "好友ID: " << fid;
                if (blocked) std::cout << " [已拉黑]";
                std::cout << std::endl;
            }
            break;
        }

        case FRIEND_REQUEST_ACTION_RESP:
            std::cout << ", 好友请求处理完成" << std::endl;
            break;

        case FRIEND_REQUEST_RESP: {
            bool requestOk = buf[sizeof(r)];
            if (!requestOk) {
                std::cout << "你和该用户之间已经有待确认的好友请求，不能重复发送" << std::endl;
            } else {
                std::cout << "好友请求已成功发送" << std::endl;
            }
            break;
        }

        case CREATE_GROUP_RESP: {
            bool groupOk = buf[sizeof(r)];
            if (groupOk) {
                std::cout << "群组创建成功！" << std::endl;
            } else {
                std::cout << "群组创建失败，请检查群组名称是否已存在。" << std::endl;
            }
            break;
        }
        case PRIVATE_MSG_RESP: {
            bool msgOk = buf[sizeof(r)];
            if (msgOk) {
                std::cout << "私聊消息发送成功" << std::endl;
            } else {
                std::cout << "私聊消息发送失败" << std::endl;
            }
            break;
        }
    }

    std::cout << std::endl;
}

void handleSigint(int) {
    if (currentUserId >= 0) {
        PacketHeader hdr{LOGOUT_REQ, sizeof(int)};
        std::vector<uint8_t> pkt(sizeof(hdr) + sizeof(int));
        memcpy(pkt.data(), &hdr, sizeof(hdr));
        memcpy(pkt.data() + sizeof(hdr), &currentUserId, sizeof(currentUserId));

        // 发送退出登录请求
        sendto(sock, pkt.data(), pkt.size(), 0, reinterpret_cast<const sockaddr*>(&serv), sizeof(serv));
        std::cout << "\n已自动发送退出登录请求 (Ctrl+C 捕获)" << std::endl;

        // 等待服务器响应
        uint8_t buf[2048];
        socklen_t len = sizeof(serv);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&serv), &len);
        if (n <= static_cast<ssize_t>(sizeof(PacketHeader))) {
            std::cerr << "无效响应或超时，退出失败" << std::endl;
        } else {
            PacketHeader r;
            memcpy(&r, buf, sizeof(r));
            bool ok = buf[sizeof(r)];
            if (r.type == LOGOUT_RESP && ok) {
                currentUserId = -1;
                std::cout << "\n退出登录成功" << std::endl;
            }
        }
    }

    close(sock);  // 关闭 socket
    std::_Exit(0);  // 强制退出
}


int main() {
    signal(SIGINT, handleSigint);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    serv = {};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serv.sin_addr) != 1) {
        std::cerr << "SERVER_IP 无效" << std::endl;
        return 1;
    }

    while (true) {
        std::cout << "\n===== LinuxQQ 控制台客户端 =====" << std::endl;
        if (currentUserId < 0) {
            std::cout << "1-注册 2-登录 0-退出程序" << std::endl;
        } else {
            std::cout << "3-修改信息 4-注销账户 5-发请求 6-查看请求\n"
                      << "7-处理请求 8-删除好友 10-好友列表 11-退出登录\n"
                      << "12-拉黑好友 13-取消拉黑 14-创建群组 15-发送私聊消息 0-退出程序" << std::endl;
            std::cout << "当前用户ID: " << currentUserId << std::endl;
        }
        std::cout << "> ";

        int op;
        if (!(std::cin >> op)) break;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        std::vector<uint8_t> pkt;
        switch (op) {
        case 0:
            if (currentUserId >= 0) {
                PacketHeader hdr{LOGOUT_REQ, sizeof(int)};
                std::vector<uint8_t> pkt(sizeof(hdr) + sizeof(int));
                memcpy(pkt.data(), &hdr, sizeof(hdr));
                memcpy(pkt.data() + sizeof(hdr), &currentUserId, sizeof(currentUserId));
                sendto(sock, pkt.data(), pkt.size(), 0, reinterpret_cast<const sockaddr*>(&serv), sizeof(serv));
                std::cout << "已自动发送退出登录请求" << std::endl;
                currentUserId = -1;
            }
            close(sock); return 0;

        case 1: {
            std::string u, p;
            std::cout << "用户名: "; std::getline(std::cin, u);
            std::cout << "密码: ";   std::getline(std::cin, p);
            std::string body = u + '\0' + p + '\0';
            pkt = buildPacket(REGISTER_REQ, std::vector<uint8_t>(body.begin(), body.end()));
        } break;

        case 2: {
            std::string u, p;
            std::cout << "用户名: "; std::getline(std::cin, u);
            std::cout << "密码: ";   std::getline(std::cin, p);
            std::string body = u + '\0' + p + '\0';
            pkt = buildPacket(LOGIN_REQ, std::vector<uint8_t>(body.begin(), body.end()));
        } break;

        case 3: {
            if (currentUserId < 0) break;
            std::string newName, newPwd;
            std::cout << "新用户名: "; std::getline(std::cin, newName);
            std::cout << "新密码: "; std::getline(std::cin, newPwd);
            std::vector<uint8_t> body(sizeof(int));
            memcpy(body.data(), &currentUserId, sizeof(int));
            body.insert(body.end(), newName.begin(), newName.end());
            body.push_back('\0');
            body.insert(body.end(), newPwd.begin(), newPwd.end());
            body.push_back('\0');
            pkt = buildPacket(UPDATE_USER_REQ, body);
        } break;

        case 4: {
            if (currentUserId < 0) break;
            std::vector<uint8_t> body(sizeof(int));
            memcpy(body.data(), &currentUserId, sizeof(int));
            pkt = buildPacket(DELETE_USER_REQ, body);
        } break;

        case 5: {  // 发送好友请求
            if (currentUserId < 0) break;

            int fid;
            std::cout << "好友ID: "; std::cin >> fid; std::cin.ignore();

            // 构建请求数据包
            std::vector<uint8_t> body;
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&currentUserId), reinterpret_cast<uint8_t*>(&currentUserId) + sizeof(int));
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&fid), reinterpret_cast<uint8_t*>(&fid) + sizeof(int));

            // 构建请求
            pkt = buildPacket(FRIEND_REQUEST_REQ, body);
        } break;

        case 6: {
            if (currentUserId < 0) break;
            std::vector<uint8_t> body(sizeof(int));
            memcpy(body.data(), &currentUserId, sizeof(int));
            pkt = buildPacket(FRIEND_REQUEST_LIST_REQ, body);
        } break;

        case 7: {
            if (currentUserId < 0) break;
            int reqId, acc;
            std::cout << "请求ID: "; std::cin >> reqId;
            std::cout << "接受(1)/拒绝(0): "; std::cin >> acc;
            std::vector<uint8_t> body;
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&reqId), reinterpret_cast<uint8_t*>(&reqId) + sizeof(int));
            body.push_back(acc ? 1 : 0);
            pkt = buildPacket(FRIEND_REQUEST_ACTION_REQ, body);
        } break;


        case 8: {
            if (currentUserId < 0) break;
            int fid;
            std::cout << "好友ID: "; std::cin >> fid;
            std::vector<uint8_t> body;
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&currentUserId), reinterpret_cast<uint8_t*>(&currentUserId) + sizeof(int));
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&fid), reinterpret_cast<uint8_t*>(&fid) + sizeof(int));
            pkt = buildPacket(DELETE_FRIEND_REQ, body);
        } break;

        case 10: {
            if (currentUserId < 0) break;
            std::vector<uint8_t> body(sizeof(int));
            memcpy(body.data(), &currentUserId, sizeof(int));
            pkt = buildPacket(FRIEND_LIST_REQ, body);
        } break;

        case 11: {
            if (currentUserId < 0) break;
            std::vector<uint8_t> body(sizeof(int));
            memcpy(body.data(), &currentUserId, sizeof(int));
            pkt = buildPacket(LOGOUT_REQ, body);
        } break;

        case 12: {
            if (currentUserId < 0) break;
            int fid;
            std::cout << "好友ID: "; std::cin >> fid;
            std::vector<uint8_t> body;
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&currentUserId), reinterpret_cast<uint8_t*>(&currentUserId) + sizeof(int));
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&fid), reinterpret_cast<uint8_t*>(&fid) + sizeof(int));
            pkt = buildPacket(BLOCK_USER_REQ, body);
        } break;

        case 13: {
            if (currentUserId < 0) break;
            int fid;
            std::cout << "好友ID: "; std::cin >> fid;
            std::vector<uint8_t> body;
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&currentUserId), reinterpret_cast<uint8_t*>(&currentUserId) + sizeof(int));
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&fid), reinterpret_cast<uint8_t*>(&fid) + sizeof(int));
            pkt = buildPacket(UNBLOCK_USER_REQ, body);
        } break;
        
        case 14: {  // 创建群组
            if (currentUserId < 0) break;

            std::string groupName;
            std::cout << "请输入群组名称: ";
            std::getline(std::cin, groupName);

            // 构建请求数据包
            pkt.clear();
            pkt.push_back(0);  // 假设0为成功标志，这里没有返回长度和错误码
            pkt.insert(pkt.end(), groupName.begin(), groupName.end());
            pkt.push_back('\0');  // 以null结尾

            // 构建请求数据包，消息类型为 CREATE_GROUP_REQ
            pkt = buildPacket(CREATE_GROUP_REQ, pkt);
            break;
        }
        
        case 15: {  // 发送私聊消息
            if (currentUserId < 0) break;

            int receiverId;
            std::cout << "目标好友ID: "; std::cin >> receiverId; std::cin.ignore();
            std::string message;
            std::cout << "消息内容: "; std::getline(std::cin, message);

            // 构建私聊消息请求数据包
            std::vector<uint8_t> body;
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&currentUserId), reinterpret_cast<uint8_t*>(&currentUserId) + sizeof(int));
            body.insert(body.end(), reinterpret_cast<uint8_t*>(&receiverId), reinterpret_cast<uint8_t*>(&receiverId) + sizeof(int));
            body.insert(body.end(), message.begin(), message.end());

            // 构建请求数据包，消息类型为 PRIVATE_MSG_REQ
            pkt = buildPacket(PRIVATE_MSG_REQ, body);
            break;
        }

        default:
            std::cout << "无效操作码" << std::endl;
            continue;
        }
        sendRequest(pkt);
    }
    return 0;
}