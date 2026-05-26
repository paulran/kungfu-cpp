#include "rest_client.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace kf {

RestClient::RestClient(QObject *parent)
    : QObject(parent)
    , nam_(new QNetworkAccessManager(this))
    , base_url_("http://127.0.0.1:8080/api/v1")
{
}

void RestClient::setBaseUrl(const QString &url) {
    base_url_ = url;
}

QNetworkRequest RestClient::makeRequest(const QString &path) const {
    QNetworkRequest req(QUrl(base_url_ + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!token_.isEmpty()) {
        req.setRawHeader("Authorization", ("Bearer " + token_).toUtf8());
    }
    return req;
}

void RestClient::login(const QString &username, const QString &password) {
    QJsonObject body;
    body["username"] = username;
    body["password"] = password;

    auto *reply = nam_->post(makeRequest("/auth/login"), QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit loginFailed(reply->errorString());
            return;
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto obj = doc.object();
        if (obj.contains("error")) {
            emit loginFailed(obj["error"].toString());
        } else {
            token_ = obj["token"].toString();
            emit loginSuccess(token_);
        }
    });
}

void RestClient::getSystemStatus() {
    auto *reply = nam_->get(makeRequest("/system/status"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("system/status", reply->errorString());
            return;
        }
        auto arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVector<ProcessInfo> result;
        for (const auto &v : arr) {
            auto o = v.toObject();
            ProcessInfo p;
            p.uid = static_cast<uint32_t>(o["uid"].toDouble());
            p.category = static_cast<Category>(o["category"].toInt());
            p.group = o["group"].toString();
            p.name = o["name"].toString();
            p.mode = o["mode"].toInt();
            p.broker_state = static_cast<BrokerState>(o["broker_state"].toInt(-1));
            result.append(p);
        }
        emit systemStatusReceived(result);
    });
}

void RestClient::getAccounts() {
    auto *reply = nam_->get(makeRequest("/accounts"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("accounts", reply->errorString());
            return;
        }
        auto arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVector<AccountInfo> result;
        for (const auto &v : arr) {
            auto o = v.toObject();
            AccountInfo a;
            a.uid = static_cast<uint32_t>(o["uid"].toDouble());
            a.source = o["source"].toString();
            a.account_id = o["account_id"].toString();
            a.state = static_cast<BrokerState>(o["state"].toInt());
            result.append(a);
        }
        emit accountsReceived(result);
    });
}

void RestClient::getAccountAssets(const QString &account_id) {
    auto *reply = nam_->get(makeRequest("/accounts/" + account_id + "/assets"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("accounts/assets", reply->errorString());
            return;
        }
        auto o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.contains("error")) {
            emit requestError("accounts/assets", o["error"].toString());
            return;
        }
        Asset a;
        a.account_id = o["account_id"].toString();
        a.initial_equity = o["initial_equity"].toDouble();
        a.static_equity = o["static_equity"].toDouble();
        a.dynamic_equity = o["dynamic_equity"].toDouble();
        a.available = o["available"].toDouble();
        a.margin = o["margin"].toDouble();
        a.frozen_cash = o["frozen_cash"].toDouble();
        a.frozen_margin = o["frozen_margin"].toDouble();
        a.frozen_fee = o["frozen_fee"].toDouble();
        a.realized_pnl = o["realized_pnl"].toDouble();
        a.unrealized_pnl = o["unrealized_pnl"].toDouble();
        emit accountAssetsReceived(a);
    });
}

void RestClient::getAccountPositions(const QString &account_id) {
    auto *reply = nam_->get(makeRequest("/accounts/" + account_id + "/positions"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("accounts/positions", reply->errorString());
            return;
        }
        auto arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVector<Position> result;
        for (const auto &v : arr) {
            auto o = v.toObject();
            Position p;
            p.instrument_id = o["instrument_id"].toString();
            p.exchange_id = o["exchange_id"].toString();
            p.direction = static_cast<Direction>(o["direction"].toInt());
            p.volume = static_cast<int64_t>(o["volume"].toDouble());
            p.yesterday_volume = static_cast<int64_t>(o["yesterday_volume"].toDouble());
            p.avg_open_price = o["avg_open_price"].toDouble();
            p.position_cost = o["position_cost"].toDouble();
            p.unrealized_pnl = o["unrealized_pnl"].toDouble();
            p.realized_pnl = o["realized_pnl"].toDouble();
            result.append(p);
        }
        emit accountPositionsReceived(result);
    });
}

void RestClient::getOrders() {
    auto *reply = nam_->get(makeRequest("/orders"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("orders", reply->errorString());
            return;
        }
        auto arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVector<Order> result;
        for (const auto &v : arr) {
            auto o = v.toObject();
            Order ord;
            ord.order_id = static_cast<uint64_t>(o["order_id"].toDouble());
            ord.instrument_id = o["instrument_id"].toString();
            ord.exchange_id = o["exchange_id"].toString();
            ord.limit_price = o["limit_price"].toDouble();
            ord.frozen_price = o["frozen_price"].toDouble();
            ord.volume = static_cast<int64_t>(o["volume"].toDouble());
            ord.volume_traded = static_cast<int64_t>(o["volume_traded"].toDouble());
            ord.volume_left = static_cast<int64_t>(o["volume_left"].toDouble());
            ord.status = static_cast<OrderStatus>(o["status"].toInt());
            ord.side = static_cast<Side>(o["side"].toInt());
            ord.offset = static_cast<Offset>(o["offset"].toInt());
            ord.insert_time = static_cast<int64_t>(o["insert_time"].toDouble());
            ord.update_time = static_cast<int64_t>(o["update_time"].toDouble());
            result.append(ord);
        }
        emit ordersReceived(result);
    });
}

void RestClient::getOrder(uint64_t order_id) {
    auto *reply = nam_->get(makeRequest("/orders/" + QString::number(order_id)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("orders/get", reply->errorString());
            return;
        }
        auto o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.contains("error")) {
            emit requestError("orders/get", o["error"].toString());
            return;
        }
        Order ord;
        ord.order_id = static_cast<uint64_t>(o["order_id"].toDouble());
        ord.instrument_id = o["instrument_id"].toString();
        ord.exchange_id = o["exchange_id"].toString();
        ord.limit_price = o["limit_price"].toDouble();
        ord.frozen_price = o["frozen_price"].toDouble();
        ord.volume = static_cast<int64_t>(o["volume"].toDouble());
        ord.volume_traded = static_cast<int64_t>(o["volume_traded"].toDouble());
        ord.volume_left = static_cast<int64_t>(o["volume_left"].toDouble());
        ord.status = static_cast<OrderStatus>(o["status"].toInt());
        ord.side = static_cast<Side>(o["side"].toInt());
        ord.offset = static_cast<Offset>(o["offset"].toInt());
        ord.insert_time = static_cast<int64_t>(o["insert_time"].toDouble());
        ord.update_time = static_cast<int64_t>(o["update_time"].toDouble());
        emit orderReceived(ord);
    });
}

void RestClient::submitOrder(const OrderInput &input) {
    QJsonObject body;
    body["instrument_id"] = input.instrument_id;
    body["exchange_id"] = input.exchange_id;
    body["limit_price"] = input.limit_price;
    body["volume"] = static_cast<qint64>(input.volume);
    body["side"] = static_cast<int>(input.side);
    body["offset"] = static_cast<int>(input.offset);
    body["price_type"] = static_cast<int>(input.price_type);

    auto *reply = nam_->post(makeRequest("/orders"), QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("orders/submit", reply->errorString());
            return;
        }
        auto o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.contains("error")) {
            emit requestError("orders/submit", o["error"].toString());
            return;
        }
        auto id = static_cast<uint64_t>(o["order_id"].toDouble());
        emit orderSubmitted(id);
    });
}

void RestClient::cancelOrder(uint64_t order_id) {
    auto *reply = nam_->deleteResource(makeRequest("/orders/" + QString::number(order_id)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, order_id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("orders/cancel", reply->errorString());
            return;
        }
        auto o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.contains("error")) {
            emit requestError("orders/cancel", o["error"].toString());
            return;
        }
        emit orderCancelled(order_id);
    });
}

void RestClient::getInstruments() {
    auto *reply = nam_->get(makeRequest("/market/instruments"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("market/instruments", reply->errorString());
            return;
        }
        auto arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVector<Instrument> result;
        for (const auto &v : arr) {
            auto o = v.toObject();
            Instrument inst;
            inst.instrument_id = o["instrument_id"].toString();
            inst.exchange_id = o["exchange_id"].toString();
            inst.instrument_type = static_cast<InstrumentType>(o["instrument_type"].toInt());
            inst.price_tick = o["price_tick"].toDouble();
            inst.delivery_year = o["delivery_year"].toInt();
            inst.delivery_month = o["delivery_month"].toInt();
            inst.contract_multiplier = o["contract_multiplier"].toInt(1);
            inst.long_margin_ratio = o["long_margin_ratio"].toDouble(1.0);
            inst.short_margin_ratio = o["short_margin_ratio"].toDouble(1.0);
            result.append(inst);
        }
        emit instrumentsReceived(result);
    });
}

void RestClient::subscribe(const QString &instrument_id, const QString &exchange_id, int instrument_type) {
    QJsonObject body;
    body["instrument_id"] = instrument_id;
    body["exchange_id"] = exchange_id;
    body["instrument_type"] = instrument_type;

    auto *reply = nam_->post(makeRequest("/market/subscribe"), QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, instrument_id, exchange_id]() {
        reply->deleteLater();
        auto o = QJsonDocument::fromJson(reply->readAll()).object();
        bool ok = o.contains("status") && o["status"].toString() == "subscribed";
        emit subscribeResult(instrument_id, exchange_id, ok);
    });
}

void RestClient::unsubscribe(const QString &instrument_id, const QString &exchange_id) {
    QJsonObject body;
    body["instrument_id"] = instrument_id;
    body["exchange_id"] = exchange_id;

    auto *reply = nam_->post(makeRequest("/market/unsubscribe"), QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, instrument_id, exchange_id]() {
        reply->deleteLater();
        auto o = QJsonDocument::fromJson(reply->readAll()).object();
        bool ok = o.contains("status") && o["status"].toString() == "unsubscribed";
        emit subscribeResult(instrument_id, exchange_id, ok);
    });
}

void RestClient::getStrategies() {
    auto *reply = nam_->get(makeRequest("/strategies"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit requestError("strategies", reply->errorString());
            return;
        }
        auto arr = QJsonDocument::fromJson(reply->readAll()).array();
        QVector<StrategyInfo> result;
        for (const auto &v : arr) {
            auto o = v.toObject();
            StrategyInfo s;
            s.uid = static_cast<uint32_t>(o["uid"].toDouble());
            s.group = o["group"].toString();
            s.name = o["name"].toString();
            if (o.contains("state"))
                s.state = static_cast<BrokerState>(o["state"].toInt());
            result.append(s);
        }
        emit strategiesReceived(result);
    });
}

} // namespace kf
