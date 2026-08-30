// SPDX-License-Identifier: Apache-2.0

#include "ApiClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

// kungfu longfist types for binary frame decoding (packed POD structs).
// Only types.h is needed (not longfist.h which pulls in ringqueue + hana maps
// unused by the client, and would force a link dependency on libkungfu).
#include <kungfu/common.h>
#include <kungfu/longfist/types.h>

#include <cstring>

using namespace kungfu::longfist::types;

namespace kfclient {

namespace {

constexpr char kBinaryFrameMarker = 0x42; // 'B'

// Convert a longfist fixed char array to a QString (null-terminated storage).
template <size_t N> QString arrStr(const kungfu::array<char, N> &a) {
  return QString::fromUtf8(a.value, static_cast<int>(strnlen(a.value, N)));
}

// Convert a longfist enum to its name via the nlohmann enum serialization,
// falling back to the integer value when the enum value is unmapped.
template <typename E> QString enumStr(E value) {
  try {
    nlohmann::json j = value;
    if (j.is_string()) return QString::fromStdString(j.get<std::string>());
  } catch (...) {
  }
  return QString::number(static_cast<int>(value));
}

} // namespace

ApiClient::ApiClient(QObject *parent) : QObject(parent), socket_(new QTcpSocket(this)) {
  connect(socket_, &QTcpSocket::connected, this, &ApiClient::onConnected);
  connect(socket_, &QTcpSocket::disconnected, this, &ApiClient::onDisconnected);
  connect(socket_, &QTcpSocket::errorOccurred, this, &ApiClient::onErrorOccurred);
  connect(socket_, &QTcpSocket::readyRead, this, &ApiClient::onReadyRead);
}

ApiClient::~ApiClient() = default;

void ApiClient::connectToHost(const QString &host, quint16 port) {
  rxBuffer_.clear();
  socket_->abort();
  socket_->connectToHost(host, port);
  emit logMessage(tr("connecting to %1:%2 ...").arg(host).arg(port));
}

void ApiClient::disconnectFromHost() {
  socket_->abort();
}

bool ApiClient::isConnected() const {
  return socket_->state() == QAbstractSocket::ConnectedState;
}

void ApiClient::onConnected() {
  emit logMessage(tr("connected to server"));
  emit connected();
}

void ApiClient::onDisconnected() {
  rxBuffer_.clear();
  emit logMessage(tr("disconnected from server"));
  emit disconnected();
}

void ApiClient::onErrorOccurred(QAbstractSocket::SocketError err) {
  Q_UNUSED(err)
  emit socketError(socket_->errorString());
  emit logMessage(tr("socket error: %1").arg(socket_->errorString()));
}

quint64 ApiClient::sendRequest(const QString &method, const QJsonObject &data) {
  if (!isConnected()) {
    emit logMessage(tr("not connected, cannot send %1").arg(method));
    return 0;
  }
  quint64 id = nextRequestId_++;
  QJsonObject req;
  // The server reads request_id with j.value("request_id", 0ULL) (nlohmann),
  // which rejects JSON strings (type_error.302). Send it as a JSON number.
  // Qt6's QJsonValue(qint64) stores the integer natively (QCborValue-backed),
  // so QJsonDocument emits an integer literal with full 64-bit precision.
  req["request_id"] = static_cast<qint64>(id);
  req["method"] = method;
  req["data"] = data;

  QJsonDocument doc(req);
  QByteArray body = doc.toJson(QJsonDocument::Compact);

  // Prepend 4-byte big-endian length.
  QByteArray frame;
  frame.reserve(4 + body.size());
  quint32 len = static_cast<quint32>(body.size());
  frame.append(static_cast<char>((len >> 24) & 0xFF));
  frame.append(static_cast<char>((len >> 16) & 0xFF));
  frame.append(static_cast<char>((len >> 8) & 0xFF));
  frame.append(static_cast<char>(len & 0xFF));
  frame.append(body);

  if (socket_->write(frame) != frame.size()) {
    emit logMessage(tr("failed to send %1 (write short)").arg(method));
  } else {
    socket_->flush();
  }
  return id;
}

void ApiClient::onReadyRead() {
  rxBuffer_.append(socket_->readAll());

  // Parse as many complete length-prefixed frames as are available.
  while (rxBuffer_.size() >= 4) {
    const auto *p = reinterpret_cast<const unsigned char *>(rxBuffer_.constData());
    quint32 len = (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16) |
                  (static_cast<quint32>(p[2]) << 8) | static_cast<quint32>(p[3]);
    if (len > 64 * 1024) { // protocol sanity bound
      emit logMessage(tr("frame too large (%1 bytes), resetting connection").arg(len));
      socket_->abort();
      return;
    }
    if (static_cast<quint32>(rxBuffer_.size()) < 4 + len) break; // need more data

    QByteArray frame = rxBuffer_.mid(4, static_cast<int>(len));
    rxBuffer_.remove(0, 4 + static_cast<int>(len));
    processFrame(frame);
  }
}

void ApiClient::processFrame(const QByteArray &frame) {
  if (frame.isEmpty()) return;

  // Binary push frames start with 'B' (0x42), JSON messages with '{'.
  if (frame[0] == kBinaryFrameMarker) {
    decodeBinary(frame);
    return;
  }

  if (frame[0] == '{') {
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(frame, &err);
    if (err.error != QJsonParseError::NoError) {
      emit logMessage(tr("json parse error: %1").arg(QString::fromUtf8(frame)));
      return;
    }
    QJsonObject j = doc.object();
    QString kind = j.value("msg_type").toString();
    if (kind == "response") {
      // request_id may arrive as string (full precision) or number.
      quint64 rid = 0;
      QJsonValue ridv = j.value("request_id");
      if (ridv.isString())
        rid = ridv.toString().toULongLong();
      else if (ridv.isDouble())
        rid = static_cast<quint64>(ridv.toDouble());
      QJsonValue data = j.value("data");
      QString error;
      QJsonValue errVal = j.value("error");
      if (!errVal.isUndefined() && !errVal.isNull()) {
        if (errVal.isString())
          error = errVal.toString();
        else
          error = QString::fromUtf8(QJsonDocument(errVal.toObject()).toJson(QJsonDocument::Compact));
      }
      emit responseReceived(rid, data, error);
    } else {
      emit logMessage(QString::fromUtf8(frame));
    }
    return;
  }

  emit logMessage(tr("unknown frame start byte 0x%1").arg(static_cast<int>(frame[0]), 2, 16, QChar('0')));
}

void ApiClient::decodeBinary(const QByteArray &frame) {
  constexpr size_t header_size = sizeof(frame_header);
  if (static_cast<size_t>(frame.size()) < 1 + header_size) {
    emit logMessage(tr("binary frame too short (%1 bytes)").arg(frame.size()));
    return;
  }

  const auto *hdr = reinterpret_cast<const frame_header *>(frame.constData() + 1);
  int32_t msg_type = hdr->msg_type;
  const char *body = frame.constData() + 1 + header_size;
  size_t body_len = static_cast<size_t>(frame.size()) - 1 - header_size;

  auto enough = [&](size_t need) { return body_len >= need; };

  switch (msg_type) {
  case Quote::tag: {
    if (!enough(sizeof(Quote))) break;
    Quote q{};
    std::memcpy(&q, body, sizeof(Quote));
    QuoteInfo info;
    info.instrumentId = arrStr(q.instrument_id);
    info.exchangeId = arrStr(q.exchange_id);
    info.tradingDay = arrStr(q.trading_day);
    info.dataTime = q.data_time;
    info.lastPrice = q.last_price;
    info.preClosePrice = q.pre_close_price;
    info.openPrice = q.open_price;
    info.highPrice = q.high_price;
    info.lowPrice = q.low_price;
    info.upperLimitPrice = q.upper_limit_price;
    info.lowerLimitPrice = q.lower_limit_price;
    info.volume = q.volume;
    info.turnover = q.turnover;
    info.openInterest = q.open_interest;
    for (int i = 0; i < 10; ++i) {
      info.bidPrice.append(q.bid_price[i]);
      info.askPrice.append(q.ask_price[i]);
      info.bidVolume.append(q.bid_volume[i]);
      info.askVolume.append(q.ask_volume[i]);
    }
    emit quoteReceived(info);
    break;
  }
  case Order::tag: {
    if (!enough(sizeof(Order))) break;
    Order o{};
    std::memcpy(&o, body, sizeof(Order));
    OrderInfo info;
    info.orderId = o.order_id;
    info.instrumentId = arrStr(o.instrument_id);
    info.exchangeId = arrStr(o.exchange_id);
    info.tradingDay = arrStr(o.trading_day);
    info.insertTime = o.insert_time;
    info.updateTime = o.update_time;
    info.limitPrice = o.limit_price;
    info.volume = o.volume;
    info.volumeLeft = o.volume_left;
    info.status = enumStr(o.status);
    info.side = enumStr(o.side);
    info.offset = enumStr(o.offset);
    info.priceType = enumStr(o.price_type);
    info.errorId = o.error_id;
    info.errorMsg = arrStr(o.error_msg);
    info.externalOrderId = arrStr(o.external_order_id);
    info.commission = o.commission;
    info.tax = o.tax;
    emit orderReceived(info);
    break;
  }
  case Trade::tag: {
    if (!enough(sizeof(Trade))) break;
    Trade t{};
    std::memcpy(&t, body, sizeof(Trade));
    TradeInfo info;
    info.tradeId = t.trade_id;
    info.orderId = t.order_id;
    info.instrumentId = arrStr(t.instrument_id);
    info.exchangeId = arrStr(t.exchange_id);
    info.tradingDay = arrStr(t.trading_day);
    info.tradeTime = t.trade_time;
    info.side = enumStr(t.side);
    info.offset = enumStr(t.offset);
    info.price = t.price;
    info.volume = t.volume;
    info.commission = t.commission;
    info.tax = t.tax;
    info.externalOrderId = arrStr(t.external_order_id);
    info.externalTradeId = arrStr(t.external_trade_id);
    emit tradeReceived(info);
    break;
  }
  case Position::tag: {
    if (!enough(sizeof(Position))) break;
    Position p{};
    std::memcpy(&p, body, sizeof(Position));
    PositionInfo info;
    info.instrumentId = arrStr(p.instrument_id);
    info.exchangeId = arrStr(p.exchange_id);
    info.tradingDay = arrStr(p.trading_day);
    info.direction = enumStr(p.direction);
    info.volume = p.volume;
    info.yesterdayVolume = p.yesterday_volume;
    info.frozenTotal = p.frozen_total;
    info.lastPrice = p.last_price;
    info.avgOpenPrice = p.avg_open_price;
    info.positionCostPrice = p.position_cost_price;
    info.unrealizedPnl = p.unrealized_pnl;
    info.realizedPnl = p.realized_pnl;
    info.holderUid = p.holder_uid;
    info.ledgerCategory = enumStr(p.ledger_category);
    info.updateTime = p.update_time;
    emit positionReceived(info);
    break;
  }
  case Asset::tag: {
    if (!enough(sizeof(Asset))) break;
    Asset a{};
    std::memcpy(&a, body, sizeof(Asset));
    AssetInfo info;
    info.tradingDay = arrStr(a.trading_day);
    info.holderUid = a.holder_uid;
    info.ledgerCategory = enumStr(a.ledger_category);
    info.initialEquity = a.initial_equity;
    info.staticEquity = a.static_equity;
    info.dynamicEquity = a.dynamic_equity;
    info.realizedPnl = a.realized_pnl;
    info.unrealizedPnl = a.unrealized_pnl;
    info.avail = a.avail;
    info.marketValue = a.market_value;
    info.margin = a.margin;
    info.frozenCash = a.frozen_cash;
    info.accumulatedFee = a.accumulated_fee;
    info.updateTime = a.update_time;
    emit assetReceived(info);
    break;
  }
  case BrokerStateUpdate::tag: {
    if (!enough(sizeof(BrokerStateUpdate))) break;
    BrokerStateUpdate b{};
    std::memcpy(&b, body, sizeof(BrokerStateUpdate));
    BrokerStateInfo info;
    info.locationUid = b.location_uid;
    info.state = enumStr(b.state);
    emit brokerStateReceived(info);
    break;
  }
  default: {
    // Register / Deregister carry std::string (variable length) so we only
    // report the header; Instrument / Channel / AssetMargin as a summary.
    QString name = QStringLiteral("msg_type_%1").arg(msg_type);
    if (msg_type == Register::tag) name = QStringLiteral("Register");
    else if (msg_type == Deregister::tag) name = QStringLiteral("Deregister");
    else if (msg_type == Instrument::tag) name = QStringLiteral("Instrument");
    else if (msg_type == Channel::tag) name = QStringLiteral("Channel");
    else if (msg_type == AssetMargin::tag) name = QStringLiteral("AssetMargin");
    QString summary = QStringLiteral("gen=%1 trig=%2 src=%3 dst=%4 body=%5B")
                          .arg(hdr->gen_time)
                          .arg(hdr->trigger_time)
                          .arg(hdr->source, 8, 16, QChar('0'))
                          .arg(hdr->dest, 8, 16, QChar('0'))
                          .arg(static_cast<qulonglong>(body_len));
    emit genericBinaryReceived(name, summary);
    break;
  }
  }
}

// ---- request methods ----

quint64 ApiClient::getLocations() {
  return sendRequest(QStringLiteral("get_locations"), QJsonObject{});
}
quint64 ApiClient::getStrategies() {
  return sendRequest(QStringLiteral("get_strategies"), QJsonObject{});
}
quint64 ApiClient::getTradingDay() {
  return sendRequest(QStringLiteral("get_trading_day"), QJsonObject{});
}
quint64 ApiClient::now() {
  return sendRequest(QStringLiteral("now"), QJsonObject{});
}
quint64 ApiClient::getSubscriptions() {
  return sendRequest(QStringLiteral("get_subscriptions"), QJsonObject{});
}

quint64 ApiClient::isReadyToInteract(const QJsonObject &location) {
  QJsonObject data;
  data["location"] = location;
  return sendRequest(QStringLiteral("is_ready_to_interact"), data);
}

quint64 ApiClient::requestMarketData(const QJsonObject &mdLocation, const QString &exchange,
                                     const QString &instrument) {
  QJsonObject data;
  data["location"] = mdLocation;
  data["exchange_id"] = exchange;
  data["instrument_id"] = instrument;
  return sendRequest(QStringLiteral("request_market_data"), data);
}

quint64 ApiClient::cancelMarketData(const QJsonObject &mdLocation, const QString &exchange,
                                    const QString &instrument) {
  QJsonObject data;
  data["location"] = mdLocation;
  data["exchange_id"] = exchange;
  data["instrument_id"] = instrument;
  return sendRequest(QStringLiteral("cancel_market_data"), data);
}

quint64 ApiClient::requestPosition() {
  return sendRequest(QStringLiteral("request_position"), QJsonObject{});
}

quint64 ApiClient::issueOrder(const QJsonObject &tdLocation, const QJsonObject &orderFields) {
  QJsonObject data;
  data["location"] = tdLocation;
  for (auto it = orderFields.begin(); it != orderFields.end(); ++it) {
    data[it.key()] = it.value();
  }
  return sendRequest(QStringLiteral("issue_order"), data);
}

quint64 ApiClient::cancelOrder(const QJsonObject &tdLocation, quint64 orderId) {
  QJsonObject data;
  data["location"] = tdLocation;
  // order_id is a uint64 that frequently exceeds 2^53 (its high 32 bits come
  // from client_id^dest). Send it as a JSON number via QJsonValue(qint64),
  // which Qt6 stores natively and serializes as an integer literal, preserving
  // full precision. The server's longfist parse() requires a number (rejects
  // strings the same way request_id does).
  data["order_id"] = static_cast<qint64>(orderId);
  return sendRequest(QStringLiteral("cancel_order"), data);
}

quint64 ApiClient::subscribeStrategy(const QJsonObject &strategyLocation) {
  QJsonObject data;
  data["location"] = strategyLocation;
  return sendRequest(QStringLiteral("subscribe_strategy"), data);
}

quint64 ApiClient::unsubscribeStrategy(const QJsonObject &strategyLocation) {
  QJsonObject data;
  data["location"] = strategyLocation;
  return sendRequest(QStringLiteral("unsubscribe_strategy"), data);
}

quint64 ApiClient::startStrategy(const QJsonObject &startFields) {
  return sendRequest(QStringLiteral("start_strategy"), startFields);
}

quint64 ApiClient::stopStrategy(const QJsonObject &strategyLocation) {
  QJsonObject data;
  data["location"] = strategyLocation;
  return sendRequest(QStringLiteral("stop_strategy"), data);
}

} // namespace kfclient
