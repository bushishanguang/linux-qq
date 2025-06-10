// MainWindow.h
// 主界面：展示好友、群组及入口至聊天窗口
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "ui_mainwindow.h"
#include "ClientNetwork.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(ClientNetwork *network, QWidget *parent = nullptr);

private slots:
    void onReceived(const PacketHeader &hdr, const QByteArray &body);
    void on_actionAddFriend_triggered();

private:
    Ui::MainWindow ui;
    ClientNetwork *net;
};
#endif // MAINWINDOW_H