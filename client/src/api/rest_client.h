#pragma once

#include "api_types.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

namespace kf {

class RestClient : public QObject {
    Q_OBJECT
public:
    explicit RestClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url);
    QString baseUrl() const { return base_url_; }

    void login(const QString &username, const QString &password);
    void getSystemStatus();
    void getAccounts();
    void getAccountAssets(const QString &account_id);
    void getAccountPositions(const QString &account_id);
    void getOrders();
    void getOrder(uint64_t order_id);
    void submitOrder(const OrderInput &input);
    void cancelOrder(uint64_t order_id);
    void getInstruments();
    void subscribe(const QString &instrument_id, const QString &exchange_id, int instrument_type = 0);
    void unsubscribe(const QString &instrument_id, const QString &exchange_id);
    void getStrategies();

    bool isAuthenticated() const { return !token_.isEmpty(); }
    void setToken(const QString &token) { token_ = token; }

signals:
    void loginSuccess(const QString &token);
    void loginFailed(const QString &error);
    void systemStatusReceived(const QVector<ProcessInfo> &processes);
    void accountsReceived(const QVector<AccountInfo> &accounts);
    void accountAssetsReceived(const Asset &asset);
    void accountPositionsReceived(const QVector<Position> &positions);
    void ordersReceived(const QVector<Order> &orders);
    void orderReceived(const Order &order);
    void orderSubmitted(uint64_t order_id);
    void orderCancelled(uint64_t order_id);
    void instrumentsReceived(const QVector<Instrument> &instruments);
    void subscribeResult(const QString &instrument_id, const QString &exchange_id, bool success);
    void strategiesReceived(const QVector<StrategyInfo> &strategies);
    void requestError(const QString &endpoint, const QString &error);

private:
    QNetworkRequest makeRequest(const QString &path) const;
    void handleReply(QNetworkReply *reply, const QString &endpoint);

    QNetworkAccessManager *nam_;
    QString base_url_;
    QString token_;
};

} // namespace kf
