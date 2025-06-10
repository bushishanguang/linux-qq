// ChatWindow.h
// 私聊/群聊窗口：消息发送、接收与显示
#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QWidget>
#include "ui_chatwindow.h"
#include "ClientNetwork.h"

class ChatWindow : public QWidget {
    Q_OBJECT
public:
    explicit ChatWindow(int peerId, ClientNetwork *network, QWidget *parent = nullptr);

private slots:
    void on_btnSend_clicked();
    void onReceived(const PacketHeader &hdr, const QByteArray &body);

private:
    Ui::ChatWindow ui;
    int peerId;
    ClientNetwork *net;
};
#endif // CHATWINDOW_H