#pragma once

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>

// #include <fmt/format.h>
#include <nlohmann/json.hpp>

//------------------------------------------------------------------------
// workaround for using c++20 with hana-1.7.0@conan-center
#if defined(_WINDOWS) && (_MSVC_LANG > 201704L)
namespace std {
template <typename T> struct is_literal_type {};
} // namespace std
#endif
//------------------------------------------------------------------------

//------------------------------------------------------------------------
// workaround for using hana string literals
#ifdef __linux__
#define BOOST_HANA_CONFIG_ENABLE_STRING_UDL
#include <boost/hana.hpp>
using namespace boost::hana::literals;
#define HANA_STR(STR) STR##_s
#else
#include <boost/hana.hpp>
#define HANA_STR(STR) BOOST_HANA_STRING(STR)
#endif

#ifdef BOOST_HANA_WORKAROUND_MSVC_PREPROCESSOR_616033
// refer to boost hana BOOST_HANA_DEFINE_STRUCT
#define MAKE_KEY(N, ...) BOOST_HANA_PP_CONCAT(BOOST_HANA_PP_CONCAT(MAKE_KEY_IMPL_, N)(__VA_ARGS__), )
#else
#define MAKE_KEY(N, ...) BOOST_HANA_PP_CONCAT(MAKE_KEY_IMPL_, N)(__VA_ARGS__)
#endif

#define MAKE_KEY_IMPL_1(k) HANA_STR(#k)
#define MAKE_KEY_IMPL_2(k1, k2) HANA_STR(#k1), HANA_STR(#k2)
#define MAKE_KEY_IMPL_3(k1, k2, k3) HANA_STR(#k1), HANA_STR(#k2), HANA_STR(#k3)
#define MAKE_KEY_IMPL_4(k1, k2, k3, k4) HANA_STR(#k1), HANA_STR(#k2), HANA_STR(#k3), HANA_STR(#k4)

#define PK(...) boost::hana::make_tuple(MAKE_KEY(BOOST_HANA_PP_NARG(__VA_ARGS__), __VA_ARGS__))

#define PERPETUAL() boost::hana::nothing
#define TIMESTAMP(FIELD) boost::hana::just(HANA_STR(#FIELD))

static constexpr int INSTRUMENT_ID_LEN = 32;
static constexpr int ACCOUNT_ID_LEN = 32;
static constexpr int PRODUCT_ID_LEN = 128;
static constexpr int DATE_LEN = 9;
static constexpr int EXCHANGE_ID_LEN = 16;
static constexpr int TRAIDNG_PHASE_CODE_LEN = 8;
static constexpr int ERROR_MSG_LEN = 256;
static constexpr int EXTERNAL_ID_LEN = 32;
static constexpr int OPPONENT_SEAT_LEN = 16;

// for trading, different type has different minimum volume, price, accounting rules for making order
enum class InstrumentType : int8_t {
  Unknown,     // 未知
  Stock,       // 股票
  Future,      // 期货
  Bond,        // 债券
  StockOption, // 股票期权
  TechStock,   // 科技股
  Fund,        // 基金
  Index,       // 指数
  Repo,        // 回购
  Warrant,     // 认权证
  Iopt,        // 牛熊证
  Crypto,      // 数字货币
};

enum class ExecType : int8_t { Unknown, Cancel, Trade };

enum class BsFlag : int8_t { Unknown, Buy, Sell };

enum class Side : int8_t {
  Buy,                       // 买入
  Sell,                      // 卖出
  Lock,                      // 锁仓
  Unlock,                    // 解锁
  Exec,                      // 行权
  Drop,                      // 放弃行权
  Purchase,                  // 申购
  Redemption,                // 赎回
  Split,                     // 拆分
  Merge,                     // 合并
  MarginTrade,               // 融资买入
  ShortSell,                 // 融券卖出
  RepayMargin,               // 卖券还款
  RepayStock,                // 买券还券
  CashRepayMargin,           // 现金还款
  StockRepayStock,           // 现券还券
  SurplusStockTransfer,      // 余券划转
  GuaranteeStockTransferIn,  // 担保品转入
  GuaranteeStockTransferOut, // 担保品转出
  Unknown = 99
};

enum class Offset : int8_t { Open, Close, CloseToday, CloseYesterday };

enum class HedgeFlag : int8_t { Speculation, Arbitrage, Hedge, Covered };

enum class OrderActionFlag : int8_t {
  Cancel,
};

enum class PriceType : int8_t {
  Limit, // 限价,证券通用
  Any, // 市价，证券通用，对于股票上海为最优五档剩余撤销，深圳为即时成交剩余撤销，建议客户采用
  FakBest5,    // 上海深圳最优五档即时成交剩余撤销，不需要报价
  ForwardBest, // 深圳本方方最优价格申报, 不需要报价
  ReverseBest, // 上海最优五档即时成交剩余转限价, 深圳对手方最优价格申报，不需要报价
  Fak,         // 深圳即时成交剩余撤销，不需要报价
  Fok,         // 深圳市价全额成交或者撤销，不需要报价
  Unknown
};

enum class VolumeCondition : int8_t { Any, Min, All };

enum class TimeCondition : int8_t { IOC, GFD, GTC };

enum class OrderStatus : int8_t {
  Unknown,
  Submitted,
  Pending,
  Cancelled,
  Error,
  Filled,
  PartialFilledNotActive,
  PartialFilledActive,
  Lost
};


template <typename V, size_t N, typename = void> struct array_to_string;

template <typename V, size_t N> struct array_to_string<V, N, std::enable_if_t<std::is_same_v<V, char>>> {
  std::string operator()(const V *v) { return std::string(v); };
};

template <typename V, size_t N> struct array_to_string<V, N, std::enable_if_t<not std::is_same_v<V, char>>> {
  std::string operator()(const V *v) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < N; i++) {
      ss << (i > 0 ? "," : "") << v[i];
    }
    ss << "]";
    return ss.str();
  };
};

template <typename ValueType>
static constexpr bool is_numeric_v = std::is_arithmetic_v<ValueType> or std::is_enum_v<ValueType>;

template <typename T, size_t N> struct kfarray {
  static constexpr size_t length = N;
  using element_type = T;
  using type = T[N];
  type value;

  kfarray() {
    if constexpr (std::is_same_v<T, char>) {
      memset(value, '\0', sizeof(value));
    } else {
      memset(value, 0, sizeof(value));
    }
  }

  explicit kfarray(const T *t) { memcpy(value, t, sizeof(value)); }

  explicit kfarray(const unsigned char *t) { memcpy(value, t, sizeof(value)); }

  [[nodiscard]] size_t size() const { return N; }

  [[nodiscard]] std::string to_string() const { return array_to_string<T, N>{}(value); }

  operator T *() { return static_cast<T *>(value); }

  operator const T *() const { return static_cast<const T *>(value); }

  operator const void *() const { return static_cast<const void *>(value); }

  operator std::string() const { return to_string(); }

  T &operator[](int i) const { return const_cast<T &>(value[i]); }

  kfarray<T, N> &operator=(const T *data) {
    if (value == data) {
      return *this;
    }
    if constexpr (std::is_same_v<T, char>) {
      memcpy(value, data, strlen(data));
    } else {
      memcpy(value, data, sizeof(value));
    }
    return *this;
  }

  kfarray<T, N> &operator=(const kfarray<T, N> &other) { return operator=(other.value); }
};


template <typename DataType> struct kfdata {
  static constexpr bool reflect = true;

  kfdata() {
    boost::hana::for_each(boost::hana::accessors<DataType>(), [&, this](auto it) {
      auto accessor = boost::hana::second(it);
      auto &v = accessor(*const_cast<DataType *>(reinterpret_cast<const DataType *>(this)));
      init_member(v);
    });
  }

  void parse(const char *address, const uint32_t length) {
    // std::string content(address, length);
    // nlohmann::json jobj = nlohmann::json::parse(content);
    // boost::hana::for_each(boost::hana::accessors<DataType>(), [&, this](auto it) {
    //   auto name = boost::hana::first(it);
    //   auto accessor = boost::hana::second(it);
    //   auto &j = jobj[name.c_str()];
    //   auto &v = accessor(*const_cast<DataType *>(reinterpret_cast<const DataType *>(this)));
    //   // restore_from_json(j, v);
    // });
  }

  [[nodiscard]] std::string to_string() const {
    nlohmann::json j = {};
    boost::hana::for_each(boost::hana::accessors<DataType>(), [&, this](auto it) {
      auto name = boost::hana::first(it);
      auto accessor = boost::hana::second(it);
      j[name.c_str()] = accessor(*reinterpret_cast<const DataType *>(this));
    });
    return j.dump(-1, ' ', false, nlohmann::json::basic_json::error_handler_t::replace);
  }

  explicit operator std::string() const { return to_string(); }

private:
  template <typename V> static std::enable_if_t<is_numeric_v<V>> init_member(V &v) { v = static_cast<V>(0); }

  template <typename V> static std::enable_if_t<not is_numeric_v<V>> init_member(V &) {}
};

template <typename> struct member_pointer_trait;

template <template <typename MemberPtr, MemberPtr ptr> typename T, typename MemberPtr, MemberPtr ptr>
struct member_pointer_trait<T<MemberPtr, ptr>> {
  static constexpr MemberPtr pointer() { return ptr; }
};


#define KF_DEFINE_DATA_TYPE(NAME, TAG, PRIMARY_KEYS, TIMESTAMP_KEY, ...)                                               \
  struct NAME : public kfdata<NAME> {                                                                            \
    static constexpr int32_t tag = TAG;                                                                                \
    static constexpr auto type_name = HANA_STR(#NAME);                                                                 \
    static constexpr auto primary_keys = PRIMARY_KEYS;                                                                 \
    static constexpr auto timestamp_key = TIMESTAMP_KEY;                                                               \
    static constexpr bool has_timestamp = boost::hana::is_just(TIMESTAMP_KEY);                                         \
    static constexpr bool has_data = true;                                                                             \
    NAME(){};                                                                                                          \
    explicit NAME(const char *address, const uint32_t length) { parse(address, length); };                             \
    explicit NAME(const std::string &text) : NAME(text.c_str(), text.length()){};                                      \
    BOOST_HANA_DEFINE_STRUCT(NAME, __VA_ARGS__);                                                                       \
  }

#define KF_DEFINE_PACK_TYPE(NAME, TAG, PRIMARY_KEYS, TIMESTAMP_KEY, ...)    \
  KF_DEFINE_DATA_TYPE(NAME, TAG, PRIMARY_KEYS, TIMESTAMP_KEY, __VA_ARGS__)


KF_DEFINE_PACK_TYPE(                                         //
    Quote, 101, PK(instrument_id, exchange_id), PERPETUAL(), //
    (kfarray<char, DATE_LEN>, trading_day),            // 交易日

    (int64_t, data_time), // 数据生成时间

    (kfarray<char, INSTRUMENT_ID_LEN>, instrument_id), // 合约ID
    (kfarray<char, EXCHANGE_ID_LEN>, exchange_id),     // 交易所ID

    (InstrumentType, instrument_type), // 合约类型

    (double, pre_close_price),      // 昨收价
    (double, pre_settlement_price), // 昨结价

    (double, last_price), // 最新价
    (int64_t, volume),    // 数量
    (double, turnover),   // 成交金额

    (double, pre_open_interest), // 昨持仓量
    (double, open_interest),     // 持仓量

    (double, open_price), // 今开盘
    (double, high_price), // 最高价
    (double, low_price),  // 最低价

    (double, upper_limit_price), // 涨停板价
    (double, lower_limit_price), // 跌停板价

    (double, close_price),      // 收盘价
    (double, settlement_price), // 结算价
    (double, iopv),             // 基金实时参考净值

    (kfarray<double, 10>, bid_price),   // 申买价
    (kfarray<double, 10>, ask_price),   // 申卖价
    (kfarray<int64_t, 10>, bid_volume), // 申买量
    (kfarray<int64_t, 10>, ask_volume), // 申卖量
    (kfarray<char, TRAIDNG_PHASE_CODE_LEN>, trading_phase_code)
    // 标的状态, 上交所用四位, 深交所用两位
    //************************************上海现货行情交易状态***************************************************************
    // 该字段为8位字符数组,左起每位表示特定的含义,无定义则填空格。
    // 第0位:‘S’表示启动(开市前)时段,‘C’表示集合竞价时段,‘T’表示连续交易时段,
    // ‘E’表示闭市时段 ,‘P’表示临时停牌,
    // ‘M’表示可恢复交易的熔断(盘中集合竞价),‘N’表示不可恢复交易的熔断(暂停交易至闭市)
    // ‘U’表示收盘集合竞价
    // 第1位:‘0’表示此产品不可正常交易,‘1’表示此产品可正常交易。
    // 第2位:‘0’表示未上市,‘1’表示已上市
    // 第3位:‘0’表示此产品在当前时段不接受进行新订单申报,‘1’ 表示此产品在当前时段可接受进行新订单申报。

    //************************************深圳现货行情交易状态***************************************************************
    // 第 0位:‘S’= 启动(开市前)‘O’= 开盘集合竞价‘T’= 连续竞价‘B’= 休市‘C’= 收盘集合竞价‘E’= 已闭市‘H’= 临时停牌‘A’=
    // 盘后交易‘V’=波动性中断 第 1位:‘0’= 正常状态 ‘1’= 全天停牌
);


KF_DEFINE_PACK_TYPE(                                           //
    Order, 203, PK(order_id), TIMESTAMP(insert_time),          //
    (uint64_t, order_id),                                      // 订单ID
    (kfarray<char, EXTERNAL_ID_LEN>, external_order_id), // 柜台订单id
    (uint64_t, parent_id),                                     // 母单号

    (int64_t, insert_time), // 订单写入时间
    (int64_t, update_time), // 订单更新时间

    (kfarray<char, DATE_LEN>, trading_day), // 交易日

    (kfarray<char, INSTRUMENT_ID_LEN>, instrument_id), // 合约ID
    (kfarray<char, EXCHANGE_ID_LEN>, exchange_id),     // 交易所ID

    (InstrumentType, instrument_type), // 合约类型

    (double, limit_price),  // 价格
    (double, frozen_price), // 冻结价格, 市价单冻结价格为0

    (int64_t, volume),      // 数量
    (int64_t, volume_left), // 剩余数量

    (double, tax),        // 税
    (double, commission), // 手续费

    (OrderStatus, status), // 订单状态

    (int32_t, error_id),                             // 错误ID
    (kfarray<char, ERROR_MSG_LEN>, error_msg), // 错误信息

    (bool, is_swap),                            // 互换单
    (Side, side),                        // 买卖方向
    (Offset, offset),                    // 开平方向
    (HedgeFlag, hedge_flag),             // 投机套保标识
    (PriceType, price_type),             // 价格类型
    (VolumeCondition, volume_condition), // 成交量类型
    (TimeCondition, time_condition)      // 成交时间类型
);
