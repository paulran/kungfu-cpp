# Kungfu-CPP 重写设计方案

---

## 一、项目概述

本项目旨在使用纯 C/C++ 重写 [kungfu-origin](https://github.com/kungfu-origin/kungfu) 量化交易执行系统，基于 C++20 标准，参考原架构设计并进行优化。

### 1.1 设计目标

| 目标 | 说明 |
|------|------|
| **高性能** | 微秒级系统响应，纳秒级时间精度 |
| **低延迟** | 基于共享内存的进程间通信 |
| **可扩展** | 支持多种交易所接口扩展 |
| **跨平台** | 支持 Windows、Linux、macOS |
| **模块化** | 清晰的模块划分和接口定义 |

### 1.2 架构演进

```
┌─────────────────────────────────────────────────────────────────┐
│                    原架构 (kungfu-origin)                       │
│  Electron UI + PM2 + C++ Core + Python 扩展                    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    新架构 (kungfu-cpp)                          │
│  Qt UI (独立子项目) + RESTful/WS API + C++20 Core              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 二、整体架构设计

### 2.1 系统架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              前端层                                    │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                    Qt UI (独立子项目)                            │    │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐   │    │
│  │  │  策略管理   │  │  订单监控  │  │  账户管理  │  │  系统配置  │   │    │
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘   │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ RESTful / WebSocket
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                              API 层                                   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                    API Gateway (kf-api)                          │    │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐   │    │
│  │  │ HTTP Server│  │ WebSocket │  │  认证鉴权  │  │  日志审计  │   │    │
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘   │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ Yijinjing (共享内存)
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                              核心层                                   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                         Master                                  │    │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐   │    │
│  │  │ 进程注册   │  │ 通道协调   │  │ 时间同步   │  │ 生命周期   │   │    │
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘   │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐            │
│  │  Ledger   │  │  Cached   │  │  Archive  │  │   API     │            │
│  │  (账本)    │  │  (缓存)    │  │  (归档)    │  │ (服务)    │            │
│  └───────────┘  └───────────┘  └───────────┘  └───────────┘            │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐                            │
│  │    MD     │  │    TD     │  │ Strategy  │                            │
│  │  (行情)    │  │  (交易)    │  │  (策略)    │                            │
│  └───────────┘  └───────────┘  └───────────┘                            │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                      Extensions (动态库)                        │    │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐                   │    │
│  │  │   XTP     │  │   CTP     │  │   SIM     │                   │    │
│  │  │ (实盘)    │  │ (实盘)    │  │ (模拟)    │                   │    │
│  │  └───────────┘  └───────────┘  └───────────┘                   │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 模块职责划分

| 模块 | 职责 | 可执行文件 |
|------|------|------------|
| **master** | 系统核心协调器，进程管理、通道管理、时间同步 | `master` |
| **ledger** | 账本服务，交易数据持久化、持仓管理 | `ledger` |
| **cached** | 缓存服务，高速数据访问、历史数据缓存 | `cached` |
| **archive** | 归档服务，定期清理历史数据 | `archive` |
| **md** | 行情数据服务，行情订阅与分发 | `md` |
| **td** | 交易服务，订单管理、自成交检测 | `td` |
| **strategy** | 策略引擎，策略生命周期管理、事件回调 | `strategy` |
| **api** | API 网关，RESTful/WebSocket 接口 | `kf-api` |

### 2.3 进程间通信架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                        通信机制架构                                 │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                  Yijinjing (核心通信层)                      │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │   │
│  │  │   Journal   │  │   Channel   │  │     nanomsg         │ │   │
│  │  │  (共享内存)  │  │  (通道管理)  │  │  (进程通知)         │ │   │
│  │  └─────────────┘  └─────────────┘  └─────────────────────┘ │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                            │                                       │
│          ┌─────────────────┼─────────────────┐                     │
│          ▼                 ▼                 ▼                     │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐            │
│  │   Master    │    │     MD      │    │     TD      │            │
│  └─────────────┘    └─────────────┘    └─────────────┘            │
│          │                 │                 │                      │
│          ▼                 ▼                 ▼                      │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐            │
│  │   Ledger    │    │  Strategy   │    │   Cached    │            │
│  └─────────────┘    └─────────────┘    └─────────────┘            │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 三、目录结构设计

### 3.1 项目根目录结构

```
kungfu-cpp/
├── 3rdparty/                    # 第三方依赖 (submodule)
│   ├── nanomsg/                # 进程间通信
│   ├── nlohmann/               # JSON 解析
│   ├── flatbuffers/            # 序列化库
│   ├── eigen/                  # 线性代数库
│   ├── tomlplusplus/           # TOML 配置解析
│   ├── cpprestsdk/             # REST API 框架
│   └── spdlog/                 # 日志库
├── src/                        # 核心源码
│   ├── core/                   # 核心框架
│   │   ├── yijinjing/          # 事件总线框架
│   │   ├── longfist/           # 数据类型定义
│   │   └── wingchun/           # 策略引擎核心
│   ├── services/               # 服务模块
│   │   ├── master/             # Master 服务
│   │   ├── ledger/             # Ledger 服务
│   │   ├── cached/             # Cached 服务
│   │   ├── archive/            # Archive 服务
│   │   ├── md/                 # 行情服务
│   │   ├── td/                 # 交易服务
│   │   ├── strategy/           # 策略引擎
│   │   └── api/                # API 网关
│   ├── tools/                  # 工具模块
│   │   ├── status/             # 服务状态查看工具
│   │   ├── log/                # 日志查看工具
│   │   └── config/             # 配置管理工具
│   ├── common/                 # 公共组件
│   │   ├── utils/              # 工具函数
│   │   ├── logging/            # 日志系统
│   │   └── config/             # 配置管理
│   └── extensions/             # 扩展模块
│       ├── xtp/                # XTP 交易所接口
│       ├── ctp/                # CTP 交易所接口
│       └── sim/                # 模拟交易接口
├── include/                    # 头文件
│   ├── kungfu/                 # 公共头文件
│   │   ├── yijinjing/          # 事件总线框架接口
│   │   ├── longfist/           # 数据类型定义接口
│   │   ├── wingchun/           # 策略引擎核心接口
│   │   └── common/             # 公共组件接口
├── bin/                        # 编译后可执行程序
├── config/                     # 配置文件
│   └── kungfu.toml             # 统一配置文件（包含所有服务配置）
├── tests/                      # 测试代码
│   ├── unit/                   # 单元测试
│   ├── integration/            # 集成测试
│   └── performance/            # 性能测试
├── scripts/                    # 脚本文件
│   ├── build.sh                # 编译脚本
│   └── deploy.sh               # 部署脚本
├── .gitignore                  # Git 忽略配置
├── .gitmodules                 # Git 子模块配置
├── ChangeLog.md                # 变更日志
├── CMakeLists.txt              # 主 CMake 配置
└── README.md                   # 项目说明
```

### 3.2 目录职责说明

| 目录 | 职责 | 说明 |
|------|------|------|
| `3rdparty/` | 第三方依赖 | 作为 git submodule 管理（nanomsg、nlohmann、flatbuffers、eigen、tomlplusplus、cpprestsdk、spdlog） |
| `src/core/` | 核心框架 | yijinjing（事件总线）、longfist（数据类型）、wingchun（策略核心） |
| `src/services/` | 服务模块 | master、ledger、cached、archive、md、td、strategy、api |
| `src/tools/` | 工具模块 | 服务状态查看、日志查看、配置管理等运维工具 |
| `src/common/` | 公共组件 | 工具、日志、配置 |
| `src/extensions/` | 交易所扩展 | 编译为动态库 |
| `include/kungfu/` | 公共头文件 | yijinjing、longfist、wingchun、common 接口 |
| `tests/` | 测试代码 | 单元、集成、性能测试 |

---

## 四、核心模块设计

### 4.1 Master 模块

#### 4.1.1 核心职责

| 职责 | 说明 |
|------|------|
| 进程注册管理 | 接收并管理所有子进程的注册请求 |
| 通道协调 | 建立进程间的通信通道 |
| 时间同步 | 发布交易日信息和时间基准 |
| 生命周期管理 | 优雅地停止所有子进程 |

#### 4.1.2 类结构

```cpp
// master.hpp
namespace kungfu {
namespace master {

class Master : public yijinjing::hero {
public:
    Master(const location_ptr& home, bool low_latency);
    
    // 事件处理
    void react() override;
    
    // 进程管理
    void register_app(const event_ptr& event);
    void deregister_app(int64_t trigger_time, uint32_t app_uid);
    
    // 通道管理
    void on_channel_request(const event_ptr& event);
    void require_write_to(int64_t time, uint32_t source_uid, uint32_t dest_uid);
    
    // 时间同步
    void publish_trading_day();
    void write_time_reset(int64_t time, writer_ptr writer);
    
protected:
    void on_exit() override;
    
private:
    session_builder session_builder_;
    std::unordered_map<uint32_t, writer_ptr> writers_;
    std::unordered_map<uint32_t, location_ptr> locations_;
    registry registry_;
};

} // namespace master
} // namespace kungfu
```

#### 4.1.3 启动命令

```bash
# 启动 Master
./bin/master -c config/kungfu.toml --low-latency
```

### 4.2 Ledger 模块

#### 4.2.1 核心职责

| 职责 | 说明 |
|------|------|
| 账户账本管理 | 维护各账户的资产和持仓 |
| 交易数据持久化 | 订单、成交数据落地存储 |
| 手续费计算 | 根据配置计算交易手续费 |
| 实时账本更新 | 响应订单和成交事件更新账本 |

#### 4.2.2 类结构

```cpp
// ledger.hpp
namespace kungfu {
namespace ledger {

class Ledger : public yijinjing::apprentice {
public:
    Ledger(const location_ptr& home, bool low_latency);
    
    void react() override;
    
    // 账本操作
    void on_order(const event_ptr& event);
    void on_trade(const event_ptr& event);
    void on_position(const event_ptr& event);
    void on_asset(const event_ptr& event);
    
    // 手续费计算
    double calculate_commission(const Trade& trade);
    
private:
    book_keeper book_keeper_;
    commission_manager commission_manager_;
};

} // namespace ledger
} // namespace kungfu
```

### 4.3 Cached 模块

#### 4.3.1 核心职责

| 职责 | 说明 |
|------|------|
| 历史数据缓存 | K线、行情历史数据缓存 |
| 高速数据访问 | 提供低延迟数据查询接口 |
| 数据预热 | 启动时加载历史数据 |

### 4.4 MD（行情数据）模块

#### 4.4.1 核心职责

| 职责 | 说明 |
|------|------|
| 行情订阅 | 订阅指定合约的实时行情 |
| 行情分发 | 将行情数据广播到公共通道 |
| 合约管理 | 维护合约信息 |
| 连接管理 | 维护与交易所的连接状态 |

#### 4.4.2 类结构

```cpp
// md.hpp
namespace kungfu {
namespace broker {

class MarketDataVendor : public yijinjing::apprentice {
public:
    MarketDataVendor(const location_ptr& home, bool low_latency);
    
    void react() override;
    void on_start() override;
    
private:
    std::unique_ptr<MarketData> service_;
};

class MarketData {
public:
    virtual bool subscribe(const std::vector<InstrumentKey>& keys) = 0;
    virtual bool unsubscribe(const std::vector<InstrumentKey>& keys) = 0;
    virtual void on_start() = 0;
    virtual void on_custom_event(const event_ptr& event) { }
    
protected:
    void try_subscribe();
    void update_instrument(const Instrument& instrument);
    
    std::unordered_map<std::string, Instrument> instruments_;
    std::vector<InstrumentKey> instruments_to_subscribe_;
};

} // namespace broker
} // namespace kungfu
```

### 4.5 TD（交易）模块

#### 4.5.1 核心职责

| 职责 | 说明 |
|------|------|
| 订单管理 | 订单提交、撤销、状态更新 |
| 账户管理 | 获取账户资产信息 |
| 持仓管理 | 获取持仓信息 |
| 自成交检测 | 防止同一账户内的自成交 |
| 数据恢复 | 重启后恢复订单状态 |

#### 4.5.2 类结构

```cpp
// td.hpp
namespace kungfu {
namespace broker {

class TraderVendor : public yijinjing::apprentice {
public:
    TraderVendor(const location_ptr& home, bool low_latency);
    
    void react() override;
    void on_start() override;
    
private:
    std::unique_ptr<Trader> service_;
};

class Trader {
public:
    virtual bool insert_order(const event_ptr& event) = 0;
    virtual bool cancel_order(const event_ptr& event) = 0;
    virtual bool req_position() = 0;
    virtual bool req_account() = 0;
    virtual void recover() = 0;
    
protected:
    bool has_self_deal_risk(const event_ptr& event);
    void handle_batch_order_tag(const event_ptr& event);
    
    std::unordered_map<uint64_t, Order> orders_;
    std::unordered_map<uint64_t, Trade> trades_;
    bool self_deal_detect_ = true;
};

} // namespace broker
} // namespace kungfu
```

### 4.6 Strategy（策略引擎）模块

#### 4.6.1 核心职责

| 职责 | 说明 |
|------|------|
| 策略生命周期管理 | 启动前、启动后、停止前、停止后回调 |
| 事件驱动回调 | 行情、订单、成交等事件的处理 |
| 交易操作接口 | 下单、撤单、查询等操作 |
| 定时任务 | 单次定时器和周期定时器 |

#### 4.6.2 类结构

```cpp
// strategy.hpp
namespace kungfu {
namespace broker {

class StrategyRunner : public yijinjing::apprentice {
public:
    StrategyRunner(const location_ptr& home, const std::string& strategy_path, bool low_latency);
    
    void react() override;
    void on_start() override;
    
private:
    std::unique_ptr<Strategy> strategy_;
    std::unique_ptr<Context> context_;
};

class Strategy {
public:
    // 生命周期回调
    virtual void pre_start(Context* context) { }
    virtual void post_start(Context* context) { }
    virtual void pre_stop(Context* context) { }
    virtual void post_stop(Context* context) { }
    
    // 行情回调
    virtual void on_quote(Context* context, const Quote& quote, const location_ptr& location) { }
    virtual void on_bar(Context* context, const Bar& bar, const location_ptr& location) { }
    
    // 订单回调
    virtual void on_order(Context* context, const Order& order, const location_ptr& location) { }
    virtual void on_trade(Context* context, const Trade& trade, const location_ptr& location) { }
};

class Context {
public:
    // 交易操作
    virtual uint64_t insert_order(...) = 0;
    virtual uint64_t cancel_order(uint64_t order_id) = 0;
    
    // 行情订阅
    virtual void subscribe(const std::string& source, const std::vector<std::string>& instruments) = 0;
    
    // 定时任务
    virtual void add_timer(int64_t nanotime, std::function<void(event_ptr)> callback) = 0;
    virtual void add_time_interval(int64_t duration, std::function<void(event_ptr)> callback) = 0;
    
    // 时间获取
    virtual int64_t now() const = 0;
};

} // namespace broker
} // namespace kungfu
```

### 4.7 API 网关模块

#### 4.7.1 核心职责

| 职责 | 说明 |
|------|------|
| RESTful API | 提供 HTTP 接口供前端调用 |
| WebSocket | 提供实时数据推送 |
| 认证鉴权 | 用户身份验证和权限控制 |
| 日志审计 | 记录 API 调用日志 |

#### 4.7.2 API 接口设计

**账户管理**

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/v1/accounts` | GET | 获取账户列表 |
| `/api/v1/accounts/{id}` | GET | 获取账户详情 |
| `/api/v1/accounts/{id}/assets` | GET | 获取账户资产 |
| `/api/v1/accounts/{id}/positions` | GET | 获取账户持仓 |

**订单管理**

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/v1/orders` | POST | 提交订单 |
| `/api/v1/orders` | GET | 查询订单列表 |
| `/api/v1/orders/{id}` | GET | 查询订单详情 |
| `/api/v1/orders/{id}` | DELETE | 撤销订单 |

**行情订阅**

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/v1/market/subscribe` | POST | 订阅行情 |
| `/api/v1/market/unsubscribe` | POST | 取消订阅 |
| `/api/v1/market/instruments` | GET | 获取合约列表 |

**策略管理**

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/v1/strategies` | POST | 启动策略 |
| `/api/v1/strategies` | GET | 获取策略列表 |
| `/api/v1/strategies/{id}` | GET | 获取策略详情 |
| `/api/v1/strategies/{id}` | DELETE | 停止策略 |

**WebSocket 实时推送**

| 通道 | 说明 |
|------|------|
| `quote.{instrument_id}` | 实时行情推送 |
| `order.{account_id}` | 订单状态推送 |
| `trade.{account_id}` | 成交推送 |
| `position.{account_id}` | 持仓变化推送 |

#### 4.7.3 类结构

```cpp
// api.hpp
namespace kungfu {
namespace api {

class ApiGateway {
public:
    ApiGateway(const std::string& host, int port);
    
    void start();
    void stop();
    
private:
    // HTTP 处理器
    void handle_accounts(const http_request& request);
    void handle_orders(const http_request& request);
    void handle_market(const http_request& request);
    void handle_strategies(const http_request& request);
    
    // WebSocket 处理器
    void handle_ws_connect(websocket_connection connection);
    void handle_ws_message(websocket_connection connection, const std::string& message);
    
    std::unique_ptr<http_listener> http_listener_;
    std::unique_ptr<websocket_listener> ws_listener_;
    yijinjing::apprentice_ptr apprentice_;
};

} // namespace api
} // namespace kungfu
```

---

## 五、扩展机制设计

### 5.1 扩展模块结构

```
src/extensions/
├── xtp/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── trader_xtp.hpp
│   └── src/
│       ├── trader_xtp.cpp
│       └── marketdata_xtp.cpp
├── ctp/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── trader_ctp.hpp
│   └── src/
│       ├── trader_ctp.cpp
│       └── marketdata_ctp.cpp
└── sim/
    ├── CMakeLists.txt
    ├── include/
    │   └── trader_sim.hpp
    └── src/
        ├── trader_sim.cpp
        └── marketdata_sim.cpp
```

### 5.2 扩展加载机制

```cpp
// extension_manager.hpp
namespace kungfu {
namespace extension {

class ExtensionManager {
public:
    static std::unique_ptr<MarketData> load_market_data(const std::string& name, BrokerVendor& vendor);
    static std::unique_ptr<Trader> load_trader(const std::string& name, BrokerVendor& vendor);
    
private:
    static void* load_library(const std::string& name);
    static std::string get_library_path(const std::string& name);
};

} // namespace extension
} // namespace kungfu
```

### 5.3 扩展接口规范

每个扩展模块需要实现以下接口：

```cpp
// 扩展模块必须导出的函数
extern "C" {
    // MarketData 扩展
    MarketData* create_market_data(BrokerVendor* vendor);
    void destroy_market_data(MarketData* instance);
    
    // Trader 扩展
    Trader* create_trader(BrokerVendor* vendor);
    void destroy_trader(Trader* instance);
}
```

---

## 六、数据类型定义

### 6.1 核心数据类型

| 类型 | 说明 | 关键字段 |
|------|------|----------|
| **OrderInput** | 订单输入 | instrument_id, exchange_id, price, volume, side, offset |
| **Order** | 订单状态 | order_id, status, price, volume, filled_volume |
| **Trade** | 成交信息 | trade_id, order_id, price, volume |
| **Quote** | 行情快照 | instrument_id, last_price, bid_price, ask_price |
| **Bar** | K线数据 | instrument_id, open, high, low, close, volume |
| **Position** | 持仓信息 | instrument_id, direction, volume, avg_cost |
| **Asset** | 资产信息 | account_id, equity, available_cash, margin |
| **Instrument** | 合约信息 | instrument_id, exchange_id, price_tick, multiplier |

### 6.2 枚举类型

```cpp
// enums.hpp
namespace kungfu {
namespace enums {

enum class Side { Buy, Sell };
enum class Offset { Open, Close, CloseToday, CloseYesterday };
enum class PriceType { Limit, Market, BestPrice };
enum class OrderStatus { Pending, Accepted, Filled, PartialFilled, Cancelled, Error };
enum class BrokerState { Disconnected, Connecting, Connected, Ready };

} // namespace enums
} // namespace kungfu
```

---

## 七、编译与部署

### 7.1 编译流程

```bash
# 1. 克隆项目（包含 submodule）
git clone --recursive https://github.com/paulran/kungfu-cpp.git

# 2. 创建构建目录
mkdir build
cd build

# 3. 配置 CMake
cmake -DCMAKE_BUILD_TYPE=Release ..

# 4. 编译
cmake --build . --config Release

# 5. 安装
cmake --install . --config Release --prefix /your/install/path
```

### 7.2 编译产物

```
kungfu-cpp/
└── bin/                          # 可执行程序和动态库
    ├── master                    # Master 进程
    ├── ledger                    # Ledger 进程
    ├── cached                    # Cached 进程
    ├── archive                   # Archive 进程
    ├── md                        # MD 进程
    ├── td                        # TD 进程
    ├── strategy                  # Strategy 进程
    ├── kf-api                    # API 网关
    ├── kf-status                 # 服务状态查看工具
    ├── kf-log                    # 日志查看工具
    ├── kf-config                 # 配置管理工具
    ├── libxtp.so                 # XTP 交易所扩展
    ├── libctp.so                 # CTP 交易所扩展
    └── libsim.so                 # 模拟交易扩展
```

### 7.3 启动流程

```bash
# 启动核心服务（使用配置文件）
./bin/master -c config/kungfu.toml
./bin/ledger -c config/kungfu.toml
./bin/cached -c config/kungfu.toml
./bin/archive -c config/kungfu.toml

# 启动业务服务（使用配置文件或命令行参数）
./bin/md -c config/kungfu.toml -g xtp -n xtp
./bin/td -c config/kungfu.toml -g xtp -n account001
./bin/strategy -c config/kungfu.toml -n my_strategy /path/to/strategy.so

# 启动 API 网关（使用配置文件）
./bin/kf-api -c config/kungfu.toml
```

---

## 八、Qt UI 子项目设计

### 8.1 项目结构

```
kungfu-qt/
├── src/
│   ├── main.cpp
│   ├── mainwindow.cpp
│   ├── mainwindow.h
│   ├── mainwindow.ui
│   ├── widgets/
│   │   ├── order_widget.cpp
│   │   ├── position_widget.cpp
│   │   ├── strategy_widget.cpp
│   │   └── market_widget.cpp
│   ├── api/
│   │   ├── rest_client.cpp
│   │   ├── ws_client.cpp
│   │   └── models/
│   └── resources/
├── CMakeLists.txt
└── README.md
```

### 8.2 UI 模块划分

| 模块 | 功能 |
|------|------|
| **策略管理** | 策略列表、启动/停止、参数配置 |
| **订单监控** | 实时订单状态、成交记录 |
| **账户管理** | 账户列表、资产查询、持仓查询 |
| **系统配置** | 服务配置、扩展管理、日志查看 |
| **行情展示** | 实时行情、K线图表 |

### 8.3 通信协议

- **REST API**：用于同步数据查询（账户、订单列表等）
- **WebSocket**：用于实时数据推送（行情、订单状态变化等）

---

## 九、第三方依赖清单

| 依赖 | 用途 | License |
|------|------|---------|
| **nanomsg** | 进程间通信 | MIT |
| **nlohmann** | JSON 解析 | MIT |
| **flatbuffers** | 序列化库 | Apache 2.0 |
| **eigen** | 线性代数库 | MPL2 |
| **tomlplusplus** | TOML 配置解析 | MIT |
| **cpprestsdk** | REST API 框架 | MIT |
| **spdlog** | 日志库 | MIT |
| **Qt** | UI 框架（独立子项目） | LGPL 3.0 |
| **XTP API** | XTP 交易所接口（扩展模块） | 商业授权 |
| **CTP API** | CTP 交易所接口（扩展模块） | 商业授权 |

### 内部核心框架

| 模块 | 用途 |
|------|------|
| **yijinjing** | 事件总线、共享内存通信 |
| **longfist** | 数据类型定义、序列化 |
| **wingchun** | 策略引擎核心 |

---

## 十、安全与性能

### 10.1 安全考虑

| 安全项 | 措施 |
|--------|------|
| **API 认证** | JWT Token 认证机制 |
| **权限控制** | 基于角色的访问控制（RBAC） |
| **数据加密** | HTTPS/WSS 传输加密 |
| **输入验证** | 严格的参数校验 |
| **日志审计** | 完整的操作日志记录 |

### 10.2 性能优化

| 优化项 | 措施 |
|--------|------|
| **共享内存** | 使用 mmap 实现低延迟通信 |
| **无锁设计** | 减少锁竞争 |
| **批量处理** | 支持批量订单处理 |
| **内存池** | 对象复用减少分配开销 |
| **异步 IO** | 非阻塞事件驱动 |

