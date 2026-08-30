// SPDX-License-Identifier: Apache-2.0
//
// Qt client for kf_api (apps/api.cpp).
//
// Talks to the API service over a raw TCP socket using length-prefixed
// framing: [4 bytes big-endian length][payload].
//   JSON   payload starts with '{'  -> request / response
//   Binary payload starts with 'B'  -> [0x42][frame_header][raw struct]
//
// The binary structs mirror kungfu/longfist/types.h and are decoded in
// ApiClient.cpp (which links the kungfu library). The header only exposes
// Qt-friendly value structs so the UI stays free of kungfu internals.

#ifndef KF_QT_CLIENT_API_CLIENT_H
#define KF_QT_CLIENT_API_CLIENT_H

#include <QObject>
#include <QAbstractSocket>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantList>

class QTcpSocket;

namespace kfclient {

// ---- Decoded, Qt-friendly views of the pushed binary frames ----

struct QuoteInfo {
    QString instrumentId;
    QString exchangeId;
    QString tradingDay;
    qint64 dataTime = 0;
    double lastPrice = 0;
    double preClosePrice = 0;
    double openPrice = 0;
    double highPrice = 0;
    double lowPrice = 0;
    double upperLimitPrice = 0;
    double lowerLimitPrice = 0;
    qint64 volume = 0;
    double turnover = 0;
    double openInterest = 0;
    // 10-level bid/ask
    QList<double> bidPrice;
    QList<double> askPrice;
    QList<qint64> bidVolume;
    QList<qint64> askVolume;
};

struct OrderInfo {
    quint64 orderId = 0;
    QString instrumentId;
    QString exchangeId;
    QString tradingDay;
    qint64 insertTime = 0;
    qint64 updateTime = 0;
    double limitPrice = 0;
    qint64 volume = 0;
    qint64 volumeLeft = 0;
    QString status;
    QString side;
    QString offset;
    QString priceType;
    qint32 errorId = 0;
    QString errorMsg;
    QString externalOrderId;
    double commission = 0;
    double tax = 0;
};

struct TradeInfo {
    quint64 tradeId = 0;
    quint64 orderId = 0;
    QString instrumentId;
    QString exchangeId;
    QString tradingDay;
    qint64 tradeTime = 0;
    QString side;
    QString offset;
    double price = 0;
    qint64 volume = 0;
    double commission = 0;
    double tax = 0;
    QString externalOrderId;
    QString externalTradeId;
};

struct PositionInfo {
    QString instrumentId;
    QString exchangeId;
    QString tradingDay;
    QString direction;
    qint64 volume = 0;
    qint64 yesterdayVolume = 0;
    qint64 frozenTotal = 0;
    double lastPrice = 0;
    double avgOpenPrice = 0;
    double positionCostPrice = 0;
    double unrealizedPnl = 0;
    double realizedPnl = 0;
    quint32 holderUid = 0;
    QString ledgerCategory;
    qint64 updateTime = 0;
};

struct AssetInfo {
    QString tradingDay;
    quint32 holderUid = 0;
    QString ledgerCategory;
    double initialEquity = 0;
    double staticEquity = 0;
    double dynamicEquity = 0;
    double realizedPnl = 0;
    double unrealizedPnl = 0;
    double avail = 0;
    double marketValue = 0;
    double margin = 0;
    double frozenCash = 0;
    double accumulatedFee = 0;
    qint64 updateTime = 0;
};

struct BrokerStateInfo {
    quint32 locationUid = 0;
    QString state;
};

struct LocationInfo {
    QString uid;
    QString category;
    QString group;
    QString name;
    QString mode;
    QString uname;
    bool live = false;
};

/// Async API client. All requests return the assigned request id; the matching
/// response is delivered via responseReceived(). Pushed binary frames are
/// delivered via the typed signals.
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);
    ~ApiClient() override;

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    [[nodiscard]] bool isConnected() const;

public slots:
    // Each method builds the JSON request and returns the request id used.
    quint64 getLocations();
    quint64 getStrategies();
    quint64 getTradingDay();
    quint64 now();
    quint64 getSubscriptions();

    quint64 isReadyToInteract(const QJsonObject &location);
    quint64 requestMarketData(const QJsonObject &mdLocation, const QString &exchange, const QString &instrument);
    quint64 cancelMarketData(const QJsonObject &mdLocation, const QString &exchange, const QString &instrument);
    quint64 requestPosition();

    quint64 issueOrder(const QJsonObject &tdLocation, const QJsonObject &orderFields);
    quint64 cancelOrder(const QJsonObject &tdLocation, quint64 orderId);

    quint64 subscribeStrategy(const QJsonObject &strategyLocation);
    quint64 unsubscribeStrategy(const QJsonObject &strategyLocation);
    quint64 startStrategy(const QJsonObject &startFields);
    quint64 stopStrategy(const QJsonObject &strategyLocation);

signals:
    void connected();
    void disconnected();
    void socketError(const QString &message);

    void responseReceived(quint64 requestId, const QJsonValue &data, const QString &error);

    // Pushed market/trading data.
    void quoteReceived(const kfclient::QuoteInfo &quote);
    void orderReceived(const kfclient::OrderInfo &order);
    void tradeReceived(const kfclient::TradeInfo &trade);
    void positionReceived(const kfclient::PositionInfo &position);
    void assetReceived(const kfclient::AssetInfo &asset);
    void brokerStateReceived(const kfclient::BrokerStateInfo &state);
    void genericBinaryReceived(const QString &typeName, const QString &summary);

    void logMessage(const QString &message);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError err);

private:
    quint64 sendRequest(const QString &method, const QJsonObject &data);
    void processFrame(const QByteArray &frame);
    void decodeBinary(const QByteArray &frame);

    QTcpSocket *socket_;
    QByteArray rxBuffer_;
    quint64 nextRequestId_ = 1;
};

} // namespace kfclient

#endif // KF_QT_CLIENT_API_CLIENT_H
