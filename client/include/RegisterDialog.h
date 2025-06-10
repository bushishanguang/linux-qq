// RegisterDialog.h
// 注册对话框：UI 与注册请求/响应逻辑
#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QDialog>
#include "ui_register.h"
#include "ClientNetwork.h"

class RegisterDialog : public QDialog {
    Q_OBJECT
public:
    explicit RegisterDialog(ClientNetwork *network, QWidget *parent = nullptr);

private slots:
    void on_btnRegister_clicked();
    void onReceived(const PacketHeader &hdr, const QByteArray &body);

private:
    Ui::Register ui;
    ClientNetwork *net;
};
#endif // REGISTERDIALOG_H