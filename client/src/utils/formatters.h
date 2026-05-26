#pragma once

#include "api/api_types.h"
#include <QString>
#include <QDateTime>

namespace kf {
namespace fmt {

inline QString sideStr(Side s) {
    switch (s) {
    case Side::Buy:  return QStringLiteral("买入");
    case Side::Sell: return QStringLiteral("卖出");
    }
    return QStringLiteral("未知");
}

inline QString offsetStr(Offset o) {
    switch (o) {
    case Offset::Open:           return QStringLiteral("开仓");
    case Offset::Close:          return QStringLiteral("平仓");
    case Offset::CloseToday:     return QStringLiteral("平今");
    case Offset::CloseYesterday: return QStringLiteral("平昨");
    }
    return QStringLiteral("未知");
}

inline QString directionStr(Direction d) {
    switch (d) {
    case Direction::Long:  return QStringLiteral("多");
    case Direction::Short: return QStringLiteral("空");
    }
    return QStringLiteral("未知");
}

inline QString priceTypeStr(PriceType p) {
    switch (p) {
    case PriceType::Limit:     return QStringLiteral("限价");
    case PriceType::Market:    return QStringLiteral("市价");
    case PriceType::BestPrice: return QStringLiteral("最优价");
    }
    return QStringLiteral("未知");
}

inline QString orderStatusStr(OrderStatus s) {
    switch (s) {
    case OrderStatus::Unknown:                return QStringLiteral("未知");
    case OrderStatus::Submitted:              return QStringLiteral("已提交");
    case OrderStatus::Pending:                return QStringLiteral("待成交");
    case OrderStatus::Cancelled:              return QStringLiteral("已撤销");
    case OrderStatus::Error:                  return QStringLiteral("错误");
    case OrderStatus::Filled:                 return QStringLiteral("全部成交");
    case OrderStatus::PartialFilledNotActive: return QStringLiteral("部成已撤");
    case OrderStatus::PartialFilledActive:    return QStringLiteral("部分成交");
    }
    return QStringLiteral("未知");
}

inline QString brokerStateStr(BrokerState s) {
    switch (s) {
    case BrokerState::Unknown:       return QStringLiteral("未知");
    case BrokerState::Idle:          return QStringLiteral("空闲");
    case BrokerState::DisConnected:  return QStringLiteral("已断开");
    case BrokerState::Connected:     return QStringLiteral("已连接");
    case BrokerState::LoggedIn:      return QStringLiteral("已登录");
    case BrokerState::Ready:         return QStringLiteral("就绪");
    case BrokerState::LoginFailed:   return QStringLiteral("登录失败");
    }
    return QStringLiteral("未知");
}

inline QString categoryStr(Category c) {
    switch (c) {
    case Category::System:   return QStringLiteral("系统");
    case Category::TD:       return QStringLiteral("交易");
    case Category::MD:       return QStringLiteral("行情");
    case Category::Strategy: return QStringLiteral("策略");
    }
    return QStringLiteral("未知");
}

inline QString instrumentTypeStr(InstrumentType t) {
    switch (t) {
    case InstrumentType::Unknown:     return QStringLiteral("未知");
    case InstrumentType::Stock:       return QStringLiteral("股票");
    case InstrumentType::Future:      return QStringLiteral("期货");
    case InstrumentType::Bond:        return QStringLiteral("债券");
    case InstrumentType::StockOption: return QStringLiteral("期权");
    case InstrumentType::Fund:        return QStringLiteral("基金");
    case InstrumentType::Index:       return QStringLiteral("指数");
    case InstrumentType::Repo:        return QStringLiteral("回购");
    case InstrumentType::Crypto:      return QStringLiteral("数字货币");
    }
    return QStringLiteral("未知");
}

inline QString nanoTimeStr(int64_t nanos) {
    if (nanos == 0) return QStringLiteral("-");
    auto ms = nanos / 1000000;
    return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm:ss.zzz");
}

inline QString modeStr(int mode) {
    switch (mode) {
    case 0: return QStringLiteral("实盘");
    case 1: return QStringLiteral("数据");
    case 2: return QStringLiteral("回放");
    case 3: return QStringLiteral("回测");
    }
    return QStringLiteral("未知");
}

} // namespace fmt
} // namespace kf
