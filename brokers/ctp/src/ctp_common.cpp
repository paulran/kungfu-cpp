#include "ctp_common.h"

#include <kungfu/yijinjing/time.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace kungfu::wingchun::ctp {

using namespace kungfu::longfist::enums;

CtpConfig parse_config(const std::string &config_json) {
  CtpConfig config = {};
  if (config_json.empty()) {
    SPDLOG_WARN("empty config, CTP connection will fail until config is set");
    return config;
  }
  try {
    nlohmann::json json = nlohmann::json::parse(config_json);
    auto read_str = [&](const char *key, std::string &target) {
      if (json.contains(key) && json[key].is_string()) {
        target = json[key].get<std::string>();
      }
    };
    read_str("front_uri", config.front_uri);
    read_str("broker_id", config.broker_id);
    read_str("user_id", config.user_id);
    read_str("password", config.password);
    read_str("app_id", config.app_id);
    read_str("auth_code", config.auth_code);
    read_str("user_product_info", config.user_product_info);
    read_str("investor_id", config.investor_id);
    if (config.investor_id.empty()) {
      config.investor_id = config.user_id;
    }
  } catch (const std::exception &e) {
    SPDLOG_ERROR("failed to parse CTP config: {}", e.what());
  }
  return config;
}

uint64_t stable_hash64(const std::string &value) {
  uint64_t hash = 1469598103934665603ULL; // FNV-1a 64 offset basis
  for (unsigned char c : value) {
    hash ^= c;
    hash *= 1099511628211ULL; // FNV-1a 64 prime
  }
  return hash;
}

int64_t nano_from_ctp_time(const char *day, const char *hhmmss, int millisec) {
  std::string day_str(day);
  std::string time_str(hhmmss);
  if (day_str.empty() || time_str.empty()) {
    return yijinjing::time::now_in_nano();
  }
  int64_t seconds = yijinjing::time::strptime(day_str + time_str, "%Y%m%d%H%M%S");
  if (seconds < 0) {
    return yijinjing::time::now_in_nano();
  }
  return seconds + millisec * 1000000;
}

Side side_from_ctp_direction(char direction) {
  switch (direction) {
  case THOST_FTDC_D_Buy:
    return Side::Buy;
  case THOST_FTDC_D_Sell:
    return Side::Sell;
  default:
    SPDLOG_ERROR("unknown direction: {}", direction);
    return Side::Unknown;
  }
}

char ctp_direction_from_side(Side side) {
  switch (side) {
  case Side::Buy:
    return THOST_FTDC_D_Buy;
  case Side::Sell:
    return THOST_FTDC_D_Sell;
  default:
    return '\0';
  }
}

Offset offset_from_ctp(char offset_flag) {
  switch (offset_flag) {
  case THOST_FTDC_OF_Open:
    return Offset::Open;
  case THOST_FTDC_OF_Close:
    return Offset::Close;
  case THOST_FTDC_OF_CloseToday:
    return Offset::CloseToday;
  case THOST_FTDC_OF_CloseYesterday:
    return Offset::CloseYesterday;
  default:
    return Offset::Open;
  }
}

char ctp_offset_from_offset(Offset offset) {
  switch (offset) {
  case Offset::Open:
    return THOST_FTDC_OF_Open;
  case Offset::Close:
    return THOST_FTDC_OF_Close;
  case Offset::CloseToday:
    return THOST_FTDC_OF_CloseToday;
  case Offset::CloseYesterday:
    return THOST_FTDC_OF_CloseYesterday;
  default:
    return THOST_FTDC_OF_Open;
  }
}

HedgeFlag hedge_flag_from_ctp(char hedge_flag) {
  switch (hedge_flag) {
  case THOST_FTDC_HF_Speculation:
    return HedgeFlag::Speculation;
  case THOST_FTDC_HF_Arbitrage:
    return HedgeFlag::Arbitrage;
  case THOST_FTDC_HF_Hedge:
    return HedgeFlag::Hedge;
  default:
    return HedgeFlag::Speculation;
  }
}

char ctp_hedge_flag_from(HedgeFlag hedge_flag) {
  switch (hedge_flag) {
  case HedgeFlag::Speculation:
    return THOST_FTDC_HF_Speculation;
  case HedgeFlag::Arbitrage:
    return THOST_FTDC_HF_Arbitrage;
  case HedgeFlag::Hedge:
  case HedgeFlag::Covered:
    return THOST_FTDC_HF_Hedge;
  default:
    return THOST_FTDC_HF_Speculation;
  }
}

OrderStatus order_status_from_ctp(char order_status) {
  switch (order_status) {
  case THOST_FTDC_OST_AllTraded:
    return OrderStatus::Filled;
  case THOST_FTDC_OST_PartTradedQueueing:
    return OrderStatus::PartialFilledActive;
  case THOST_FTDC_OST_PartTradedNotQueueing:
    return OrderStatus::PartialFilledNotActive;
  case THOST_FTDC_OST_NoTradeQueueing:
    return OrderStatus::Submitted;
  case THOST_FTDC_OST_NoTradeNotQueueing:
    return OrderStatus::Cancelled;
  case THOST_FTDC_OST_Canceled:
    return OrderStatus::Cancelled;
  case THOST_FTDC_OST_Unknown:
    return OrderStatus::Unknown;
  case THOST_FTDC_OST_NotTouched:
    return OrderStatus::Pending;
  case THOST_FTDC_OST_Touched:
    return OrderStatus::Pending;
  default:
    SPDLOG_ERROR("unknown order status: {}", order_status);
    return OrderStatus::Unknown;
  }
}

char ctp_order_price_type_from(PriceType price_type) {
  switch (price_type) {
  case PriceType::Limit:
    return THOST_FTDC_OPT_LimitPrice;
  case PriceType::Any:
  case PriceType::Fak:
  case PriceType::Fok:
    return THOST_FTDC_OPT_AnyPrice;
  default:
    return THOST_FTDC_OPT_LimitPrice;
  }
}

char ctp_time_condition_from(PriceType price_type, TimeCondition time_condition) {
  if (price_type == PriceType::Any || price_type == PriceType::Fak || price_type == PriceType::Fok) {
    return THOST_FTDC_TC_IOC;
  }
  switch (time_condition) {
  case TimeCondition::IOC:
    return THOST_FTDC_TC_IOC;
  case TimeCondition::GFD:
    return THOST_FTDC_TC_GFD;
  case TimeCondition::GTC:
    return THOST_FTDC_TC_GTC;
  default:
    return THOST_FTDC_TC_GFD;
  }
}

char ctp_volume_condition_from(PriceType price_type, VolumeCondition volume_condition) {
  if (price_type == PriceType::Fok) {
    return THOST_FTDC_VC_CV;
  }
  switch (volume_condition) {
  case VolumeCondition::Any:
    return THOST_FTDC_VC_AV;
  case VolumeCondition::Min:
    return THOST_FTDC_VC_MV;
  case VolumeCondition::All:
    return THOST_FTDC_VC_CV;
  default:
    return THOST_FTDC_VC_AV;
  }
}

} // namespace kungfu::wingchun::ctp
