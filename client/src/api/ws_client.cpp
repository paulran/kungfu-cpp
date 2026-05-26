#include "ws_client.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDataStream>

namespace kf {

WsClient::WsClient(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , reconnect_timer_(new QTimer(this))
{
    reconnect_timer_->setInterval(3000);
    reconnect_timer_->setSingleShot(true);

    connect(socket_, &QTcpSocket::connected, this, &WsClient::onSocketConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &WsClient::onSocketDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &WsClient::onSocketReadyRead);
    connect(reconnect_timer_, &QTimer::timeout, this, &WsClient::onReconnectTimer);
}

void WsClient::setUrl(const QString &url) {
    url_ = QUrl(url);
}

void WsClient::connectToServer() {
    intentional_disconnect_ = false;
    handshake_done_ = false;
    read_buffer_.clear();
    int port = url_.port(80);
    socket_->connectToHost(url_.host(), port);
}

void WsClient::disconnectFromServer() {
    intentional_disconnect_ = true;
    reconnect_timer_->stop();
    socket_->close();
}

bool WsClient::isConnected() const {
    return socket_->state() == QAbstractSocket::ConnectedState && handshake_done_;
}

void WsClient::subscribe(const QString &channel) {
    subscriptions_.insert(channel);
    if (isConnected()) {
        QJsonObject msg;
        msg["action"] = QStringLiteral("subscribe");
        msg["channel"] = channel;
        sendTextFrame(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}

void WsClient::unsubscribe(const QString &channel) {
    subscriptions_.remove(channel);
    if (isConnected()) {
        QJsonObject msg;
        msg["action"] = QStringLiteral("unsubscribe");
        msg["channel"] = channel;
        sendTextFrame(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}

void WsClient::subscribeAll() {
    subscribe("*");
}

void WsClient::onSocketConnected() {
    sendHandshake();
}

void WsClient::onSocketDisconnected() {
    handshake_done_ = false;
    read_buffer_.clear();
    emit disconnected();
    if (!intentional_disconnect_) {
        reconnect_timer_->start();
    }
}

void WsClient::onReconnectTimer() {
    if (socket_->state() == QAbstractSocket::UnconnectedState && !intentional_disconnect_) {
        connectToServer();
    }
}

void WsClient::sendHandshake() {
    QByteArray keyBytes(16, 0);
    for (int i = 0; i < 16; ++i)
        keyBytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    ws_key_ = keyBytes.toBase64();

    QString path = url_.path().isEmpty() ? "/" : url_.path();
    QByteArray request;
    request.append("GET " + path.toUtf8() + " HTTP/1.1\r\n");
    request.append("Host: " + url_.host().toUtf8() + ":" + QByteArray::number(url_.port(80)) + "\r\n");
    request.append("Upgrade: websocket\r\n");
    request.append("Connection: Upgrade\r\n");
    request.append("Sec-WebSocket-Key: " + ws_key_.toUtf8() + "\r\n");
    request.append("Sec-WebSocket-Version: 13\r\n");
    request.append("\r\n");
    socket_->write(request);
}

bool WsClient::parseHandshakeResponse() {
    int headerEnd = read_buffer_.indexOf("\r\n\r\n");
    if (headerEnd < 0) return false;

    QByteArray header = read_buffer_.left(headerEnd);
    read_buffer_ = read_buffer_.mid(headerEnd + 4);

    if (header.contains("101") && header.toLower().contains("upgrade")) {
        handshake_done_ = true;
        emit connected();
        resubscribeAll();
        return true;
    }

    socket_->close();
    return false;
}

void WsClient::onSocketReadyRead() {
    read_buffer_.append(socket_->readAll());

    if (!handshake_done_) {
        if (!parseHandshakeResponse()) return;
    }

    // Parse WebSocket frames
    while (read_buffer_.size() >= 2) {
        quint8 b0 = static_cast<quint8>(read_buffer_[0]);
        quint8 b1 = static_cast<quint8>(read_buffer_[1]);

        bool masked = (b1 & 0x80) != 0;
        quint64 payload_len = b1 & 0x7F;
        int header_size = 2;

        if (payload_len == 126) {
            if (read_buffer_.size() < 4) return;
            payload_len = (static_cast<quint8>(read_buffer_[2]) << 8) |
                          static_cast<quint8>(read_buffer_[3]);
            header_size = 4;
        } else if (payload_len == 127) {
            if (read_buffer_.size() < 10) return;
            payload_len = 0;
            for (int i = 0; i < 8; ++i)
                payload_len = (payload_len << 8) | static_cast<quint8>(read_buffer_[2 + i]);
            header_size = 10;
        }

        if (masked) header_size += 4;

        quint64 total = header_size + payload_len;
        if (static_cast<quint64>(read_buffer_.size()) < total) return;

        QByteArray payload = read_buffer_.mid(header_size, static_cast<int>(payload_len));

        if (masked) {
            QByteArray mask_key = read_buffer_.mid(header_size - 4, 4);
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = payload[i] ^ mask_key[i % 4];
        }

        read_buffer_ = read_buffer_.mid(static_cast<int>(total));

        int opcode = b0 & 0x0F;
        if (opcode == 0x01) { // text frame
            processFrame(payload);
        } else if (opcode == 0x08) { // close
            socket_->close();
            return;
        } else if (opcode == 0x09) { // ping - send pong
            QByteArray pong;
            pong.append(static_cast<char>(0x8A));
            pong.append(static_cast<char>(0x80 | payload.size()));
            QByteArray mask(4, 0);
            for (int i = 0; i < 4; ++i)
                mask[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
            pong.append(mask);
            for (int i = 0; i < payload.size(); ++i)
                pong.append(payload[i] ^ mask[i % 4]);
            socket_->write(pong);
        }
    }
}

void WsClient::processFrame(const QByteArray &payload) {
    auto doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;
    parseMessage(doc.object());
}

void WsClient::sendTextFrame(const QByteArray &data) {
    QByteArray frame;
    frame.append(static_cast<char>(0x81)); // FIN + text opcode

    // Client must mask
    if (data.size() < 126) {
        frame.append(static_cast<char>(0x80 | data.size()));
    } else if (data.size() < 65536) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((data.size() >> 8) & 0xFF));
        frame.append(static_cast<char>(data.size() & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        quint64 len = data.size();
        for (int i = 7; i >= 0; --i)
            frame.append(static_cast<char>((len >> (i * 8)) & 0xFF));
    }

    QByteArray mask(4, 0);
    for (int i = 0; i < 4; ++i)
        mask[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    frame.append(mask);

    for (int i = 0; i < data.size(); ++i)
        frame.append(data[i] ^ mask[i % 4]);

    socket_->write(frame);
}

void WsClient::resubscribeAll() {
    for (const auto &ch : subscriptions_) {
        QJsonObject msg;
        msg["action"] = QStringLiteral("subscribe");
        msg["channel"] = ch;
        sendTextFrame(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}

void WsClient::parseMessage(const QJsonObject &msg) {
    QString channel = msg["channel"].toString();
    QJsonObject data = msg["data"].toObject();

    if (channel.startsWith("quote.")) {
        Quote q;
        q.instrument_id = data["instrument_id"].toString();
        q.exchange_id = data["exchange_id"].toString();
        q.data_time = static_cast<int64_t>(data["data_time"].toDouble());
        q.last_price = data["last_price"].toDouble();
        q.pre_close_price = data["pre_close_price"].toDouble();
        q.open_price = data["open_price"].toDouble();
        q.high_price = data["high_price"].toDouble();
        q.low_price = data["low_price"].toDouble();
        q.volume = static_cast<int64_t>(data["volume"].toDouble());
        q.turnover = data["turnover"].toDouble();
        for (int i = 0; i < 5; ++i) {
            q.bid_price[i] = data[QString("bid_price_%1").arg(i)].toDouble();
            q.bid_volume[i] = static_cast<int64_t>(data[QString("bid_volume_%1").arg(i)].toDouble());
            q.ask_price[i] = data[QString("ask_price_%1").arg(i)].toDouble();
            q.ask_volume[i] = static_cast<int64_t>(data[QString("ask_volume_%1").arg(i)].toDouble());
        }
        emit quoteReceived(q);
    } else if (channel.startsWith("order.")) {
        Order o;
        o.order_id = static_cast<uint64_t>(data["order_id"].toDouble());
        o.instrument_id = data["instrument_id"].toString();
        o.exchange_id = data["exchange_id"].toString();
        o.limit_price = data["limit_price"].toDouble();
        o.frozen_price = data["frozen_price"].toDouble();
        o.volume = static_cast<int64_t>(data["volume"].toDouble());
        o.volume_traded = static_cast<int64_t>(data["volume_traded"].toDouble());
        o.volume_left = static_cast<int64_t>(data["volume_left"].toDouble());
        o.status = static_cast<OrderStatus>(data["status"].toInt());
        o.side = static_cast<Side>(data["side"].toInt());
        o.offset = static_cast<Offset>(data["offset"].toInt());
        o.insert_time = static_cast<int64_t>(data["insert_time"].toDouble());
        o.update_time = static_cast<int64_t>(data["update_time"].toDouble());
        emit orderReceived(o);
    } else if (channel.startsWith("trade.")) {
        Trade t;
        t.trade_id = static_cast<uint64_t>(data["trade_id"].toDouble());
        t.order_id = static_cast<uint64_t>(data["order_id"].toDouble());
        t.instrument_id = data["instrument_id"].toString();
        t.exchange_id = data["exchange_id"].toString();
        t.price = data["price"].toDouble();
        t.volume = static_cast<int64_t>(data["volume"].toDouble());
        t.side = static_cast<Side>(data["side"].toInt());
        t.offset = static_cast<Offset>(data["offset"].toInt());
        t.trade_time = static_cast<int64_t>(data["trade_time"].toDouble());
        emit tradeReceived(t);
    } else if (channel.startsWith("position.")) {
        Position p;
        p.instrument_id = data["instrument_id"].toString();
        p.exchange_id = data["exchange_id"].toString();
        p.direction = static_cast<Direction>(data["direction"].toInt());
        p.volume = static_cast<int64_t>(data["volume"].toDouble());
        p.yesterday_volume = static_cast<int64_t>(data["yesterday_volume"].toDouble());
        p.avg_open_price = data["avg_open_price"].toDouble();
        p.position_cost = data["position_cost"].toDouble();
        p.unrealized_pnl = data["unrealized_pnl"].toDouble();
        p.realized_pnl = data["realized_pnl"].toDouble();
        emit positionReceived(p);
    } else if (channel.startsWith("asset.")) {
        Asset a;
        a.account_id = data["account_id"].toString();
        a.initial_equity = data["initial_equity"].toDouble();
        a.static_equity = data["static_equity"].toDouble();
        a.dynamic_equity = data["dynamic_equity"].toDouble();
        a.available = data["available"].toDouble();
        a.margin = data["margin"].toDouble();
        a.frozen_cash = data["frozen_cash"].toDouble();
        a.frozen_margin = data["frozen_margin"].toDouble();
        a.frozen_fee = data["frozen_fee"].toDouble();
        a.realized_pnl = data["realized_pnl"].toDouble();
        a.unrealized_pnl = data["unrealized_pnl"].toDouble();
        emit assetReceived(a);
    } else if (channel == "system.status") {
        ProcessInfo info;
        info.uid = static_cast<uint32_t>(data["uid"].toDouble());
        info.category = static_cast<Category>(data["category"].toInt());
        info.group = data["group"].toString();
        info.name = data["name"].toString();
        info.mode = data["mode"].toInt();
        info.broker_state = static_cast<BrokerState>(data["broker_state"].toInt(-1));
        emit systemStatusChanged(info);
    }
}

} // namespace kf
