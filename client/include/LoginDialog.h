// LoginDialog.h
// 登录对话框：UI 与登录请求/响应逻辑
#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include "ui_login.h"
#include "ClientNetwork.h"

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(ClientNetwork *network, QWidget *parent = nullptr);

private slots:
    void on_btnLogin_clicked();
    void onReceived(const PacketHeader &hdr, const QByteArray &body);

private:
    Ui::Login ui;
    ClientNetwork *net;
};
#endif // LOGINDIALOG_H