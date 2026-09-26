#pragma once

// Shared helpers for the CTP extension (kf_ctp): account config parsing and
// conversions between CTP field values and kungfu longfist enums.

#include <cfloat>
#include <string>

#include <kungfu/longfist/longfist.h>
#include <ThostFtdcUserApiDataType.h>

namespace kungfu::wingchun::ctp {

// Account config stored as the Config JSON of the md/td location. All fields
// are plain strings; SimNow example (broker_id 9999, app_id simnow_client_test,
// auth_code 0000000000000000; 7x24 fronts tcp://180.168.146.187:10202 td /
// :10212 md, trading-hours fronts :10101 td / :10111 md):
//   {"front_uri":"tcp://180.168.146.187:10202", "broker_id":"9999",
//    "user_id":"123456", "password":"...",
//    "app_id":"simnow_client_test", "auth_code":"0000000000000000"}
// Optional keys: investor_id (defaults to user_id), user_product_info,
// md_front_uri / td_front_uri (override front_uri per service).
struct CtpConfig {
  std::string front_uri;
  std::string broker_id;
  std::string user_id;
  std::string password;
  std::string app_id;
  std::string auth_code;
  std::string user_product_info;
  std::string investor_id;
};

CtpConfig parse_config(const std::string &config_json);

/// CTP uses DBL_MAX for "no price"; map it to 0.0 so consumers can check >0.
inline double ctp_price(double price) { return (price >= DBL_MAX || price <= -DBL_MAX) ? 0.0 : price; }

/// Stable 64bit hash used to synthesize kungfu order ids for orders that were
/// placed by a previous session (recovered through req_order_trade).
uint64_t stable_hash64(const std::string &value);

/// yyyymmdd + HH:MM:SS + millis -> epoch nano (local timezone, same base as
/// yijinjing::time). Returns yijinjing::time::now_in_nano() on empty inputs.
int64_t nano_from_ctp_time(const char *day, const char *hhmmss, int millisec);

longfist::enums::Side side_from_ctp_direction(char direction);
char ctp_direction_from_side(longfist::enums::Side side);

longfist::enums::Offset offset_from_ctp(char offset_flag);
char ctp_offset_from_offset(longfist::enums::Offset offset);

longfist::enums::HedgeFlag hedge_flag_from_ctp(char hedge_flag);
char ctp_hedge_flag_from(longfist::enums::HedgeFlag hedge_flag);

longfist::enums::OrderStatus order_status_from_ctp(char order_status);

char ctp_order_price_type_from(longfist::enums::PriceType price_type);
char ctp_time_condition_from(longfist::enums::PriceType price_type,
                             longfist::enums::TimeCondition time_condition);
char ctp_volume_condition_from(longfist::enums::PriceType price_type,
                               longfist::enums::VolumeCondition volume_condition);

} // namespace kungfu::wingchun::ctp
