#pragma once

#include "api_types.h"
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QSet>
#include <QUrl>
#include <QByteArray>

namespace kf {

class WsClient : public QObject {
    Q_OBJECT
public:
    explicit WsClient(QObject *parent = nullptr);

    void setUrl(const QString &url);
    void connectToServer();
    void disconnectFromServer();
    bool isConnected() const;

    void subscribe(const QString &channel);
    void unsubscribe(const QString &channel);
    void subscribeAll();

signals:
    void connected();
    void disconnected();
    void quoteReceived(const kf::Quote &quote);
    void orderReceived(const kf::Order &order);
    void tradeReceived(const kf::Trade &trade);
    void positionReceived(const kf::Position &position);
    void assetReceived(const kf::Asset &asset);
    void systemStatusChanged(const kf::ProcessInfo &info);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onReconnectTimer();

private:
    void sendHandshake();
    bool parseHandshakeResponse();
    void processFrame(const QByteArray &payload);
    void parseMessage(const QJsonObject &msg);
    void sendTextFrame(const QByteArray &data);
    void resubscribeAll();

    QTcpSocket *socket_;
    QTimer *reconnect_timer_;
    QUrl url_;
    QSet<QString> subscriptions_;
    bool intentional_disconnect_ = false;
    bool handshake_done_ = false;
    QByteArray read_buffer_;
    QString ws_key_;
};

} // namespace kf
