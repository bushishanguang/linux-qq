// ClientNetwork.h
// 封装 Qt UDP 通信：发送/接收与信号分发
#ifndef CLIENTNETWORK_H
#define CLIENTNETWORK_H

#include <QObject>
#include <QUdpSocket>
#include "Protocol.h"

class ClientNetwork : public QObject {
    Q_OBJECT
public:
    explicit ClientNetwork(QObject *parent = nullptr);
    ~ClientNetwork();

    bool init(const QString &serverIp, quint16 serverPort);
    void sendPacket(const PacketHeader &hdr, const QByteArray &body);

signals:
    void received(const PacketHeader &hdr, const QByteArray &body);

private slots:
    void onReadyRead();

private:
    QUdpSocket *socket;
    QHostAddress serverAddress;
    quint16 serverPort;
};
#endif // CLIENTNETWORK_H