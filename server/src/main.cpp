#include "Config.h"
#include "ChatServer.h"
#include <iostream>

int main() {
    ChatServer server(SERVER_PORT, DB_FILE_PATH);
    if (!server.init()) {
        std::cerr << "数据库初始化失败" << std::endl;
        return -1;
    }
    if (!server.start()) {
        std::cerr << "服务器启动失败" << std::endl;
        return -1;
    }
    return 0;
}