# Kungfu-CPP

纯 C++20 重写的 [kungfu-origin](https://github.com/kungfu-origin/kungfu) 量化交易执行系统。

## 设计目标

| 目标 | 说明 |
|------|------|
| 高性能 | 微秒级系统响应，Journal mmap 零拷贝 IPC |
| 纯 C++ | 去除 Node.js/Python/Electron，统一技术栈 |
| 多进程 | 各组件独立进程，通过 Journal 通信，NNG 协调 |
| 跨平台 | Windows / Linux / macOS |

---

## 架构

```
┌────────────┐         NNG PUB/PULL          ┌────────────┐
│  kf_master │◄─────── 注册/通道协调 ────────►│ apprentice │
└────────────┘                                └────────────┘

┌────────────┐   Journal mmap    ┌────────────┐   Journal mmap    ┌────────────┐
│   kf_md    │──── Quote ───────►│kf_strategy │──── OrderInput ──►│   kf_td    │
└────────────┘                   └────────────┘                   └────────────┘
                                       ▲                                │
                                       └─────── Order/Trade ────────────┘
                                                Journal mmap
```

### 进程职责

| 进程 | 说明 |
|------|------|
| `kf_master` | 系统协调器：进程注册、Channel 通道建立、NNG PUB/PULL |
| `kf_md` | 行情源：生成或接收行情数据，写入公开 Journal |
| `kf_td` | 交易执行：读取 OrderInput，执行交易后写回 Order/Trade |
| `kf_strategy` | 策略进程：订阅行情、下单、接收回报 |

辅助服务：

| 进程 | 说明 |
|------|------|
| `kf_ledger` | 聚合持仓/资产（多策略风控场景需要） |
| `kf_cached` | 状态恢复服务 |

---

## 核心模块

### Yijinjing — Journal IPC

基于 mmap 的零拷贝进程间通信：

- **Writer** 写入帧（FrameHeader + 结构体），atomic release fence 保证可见性
- **Reader** 通过 mmap 一致性轮询读取，无需信号通知
- **文件路径**: `{home}/journal/{category}/{group}/{name}/{mode}/{dest_uid_hex}.{page_id}.journal`
- **PUBLIC_UID = 0**: 广播通道，所有进程可读

### Longfist — 数据类型

Boost.Hana 编译期反射，结构体自动支持 Journal 序列化、JSON 转换、SQLite 映射：

- `Quote`, `OrderInput`, `Order`, `Trade`, `Position`, `Asset`
- `Subscribe`, `Channel`, `Register`, `RequestWriteTo`, `RequestReadFrom`

### Wingchun — 交易引擎

- **Strategy** 接口：`pre_start`, `post_start`, `on_quote`, `on_order`, `on_trade`, `pre_stop`
- **Context** 接口：`subscribe`, `insert_order`, `cancel_order`, `add_md`, `add_account`, `get_position`
- **Runner**: apprentice 子类，事件循环分发 Journal 帧到 Strategy
- **BrokerVendor/Service**: MD 和 TD 的 Vendor（IPC）+ Service（业务逻辑）分离
- **MatchingEngine**: 限价单撮合，支持全额/部分成交
- **BookKeeper**: 持仓/PnL 跟踪

### Practice — 进程协作

- **hero**: 事件循环基类（rxcpp observable + produce 轮询）
- **apprentice**: 子进程基类，自动注册到 Master，接收 Channel 通知

---

## 目录结构

```
kungfu-cpp/
├── 3rdparty/              # 第三方依赖
│   ├── fmt/               # 格式化库
│   ├── googletest/        # 测试框架
│   ├── hana/              # Boost.Hana 编译期反射
│   ├── nlohmann/          # JSON
│   ├── nng/               # NNG (进程间控制)
│   ├── rxcpp/             # 响应式事件循环
│   ├── spdlog/            # 日志
│   ├── sqlite3/           # SQLite
│   ├── sqlite_orm/        # ORM
│   ├── tabulate/          # 表格输出
│   └── tomlplusplus/      # TOML 配置
├── include/kungfu/        # 公共头文件
│   ├── common.h           # 配置、hash、mmap
│   ├── longfist/          # 数据类型、枚举、序列化
│   ├── wingchun/          # 策略引擎、Broker、Gateway
│   └── yijinjing/         # Journal、Locator、Practice
├── src/                   # 核心源码
│   ├── yijinjing/         # Journal 系统、NNG socket、Practice
│   └── wingchun/          # 交易引擎核心
│       ├── broker/        # BrokerVendor、BrokerClient
│       ├── gateway/sim/   # SimMD、SimTD、MatchingEngine
│       └── strategy/      # Runner、RuntimeContext、SimContext
├── apps/                  # 应用程序入口
│   ├── master.cpp         # kf_master
│   ├── md.cpp             # kf_md
│   ├── td.cpp             # kf_td
│   ├── strategy.cpp       # kf_strategy
│   ├── ledger.cpp         # kf_ledger
│   ├── cached.cpp         # kf_cached
│   └── strategies/        # 策略实现
├── examples/              # 示例代码
│   ├── producer.cpp       # Journal 生产者示例
│   ├── consumer.cpp       # Journal 消费者示例
│   ├── multi_process_strategy.cpp   # 多进程策略示例
│   ├── sim_strategy_demo.cpp        # 单进程 SIM 演示
│   ├── api_gateway_demo.cpp         # API Gateway 演示
│   └── nng_http_client.cpp          # NNG HTTP 客户端示例
├── cmake/                 # CMake 配置
├── config/                # 配置文件
│   └── supervisord.conf   # supervisord 配置
└── CMakeLists.txt
```

---

## 编译

```bash
# 前置要求: CMake 3.20+, C++20 编译器 (MSVC 2022 / GCC 11+ / Clang 14+)

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# 单独编译特定目标
make kf_master -j4
make kf_md -j4
make kf_td -j4
make kf_strategy -j4
```

### 编译产物

| 目标 | 路径 | 说明 |
|------|------|------|
| `kf_master` | `apps/` | Master 协调进程 |
| `kf_md` | `apps/` | 行情进程 |
| `kf_td` | `apps/` | 交易进程 |
| `kf_strategy` | `apps/` | 策略进程 |
| `kf_ledger` | `apps/` | 账本服务 |
| `kf_cached` | `apps/` | 缓存服务 |

---

## 运行多进程 SIM

### 使用 supervisord 启动

项目使用 supervisord 统一管理所有进程：

```bash
# 启动所有进程
supervisord -c config/supervisord.conf

# 查看进程状态
supervisorctl -c config/supervisord.conf status

# 停止所有进程
supervisorctl -c config/supervisord.conf stop all

# 重启指定进程
supervisorctl -c config/supervisord.conf restart kf_strategy
```

### supervisord 配置

[config/supervisord.conf](./config/supervisord.conf) 定义了以下进程：

| 进程 | 命令 | 优先级 | 说明 |
|------|------|--------|------|
| `kf_master` | `kf_master` | 10 | Master 协调进程 |
| `kf_cached` | `kf_cached` | 15 | 状态恢复服务 |
| `kf_ledger` | `kf_ledger` | 15 | 账本服务 |
| `kf_md_sim` | `kf_md --group sim --name sim` | 20 | SIM 行情进程 |
| `kf_td_sim` | `kf_td --group sim --name sim --source sim` | 20 | SIM 交易进程 |
| `kf_strategy` | `kf_strategy --group sim --name sim` | 30 | 策略进程 |

日志文件位于 `~/.kungfu-cpp/log/` 目录下。

---

## 策略开发

策略示例参考 [apps/strategies/strategy101.h](./apps/strategies/strategy101.h)。

```cpp
#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/wingchun/strategy/strategy.h>

using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::wingchun::strategy;

class MyStrategy : public Strategy {
public:
    void pre_start(Context_ptr &context) override {
        context->add_account("sim", "sim");
        context->subscribe("sim", {"600000"}, {"SSE"});
    }

    void on_quote(Context_ptr &context, const Quote &quote, const location_ptr &location) override {
        if (!ordered_ && quote.ask_price_0 > 0) {
            context->insert_order("600000", "SSE", quote.ask_price_0, 100,
                                 PriceType::Limit, Side::Buy, Offset::Open);
            ordered_ = true;
        }
    }

    void on_trade(Context_ptr &context, const Trade &trade, const location_ptr &location) override {
        // Handle trade
    }

private:
    bool ordered_ = false;
};
```

### 策略生命周期

| 方法 | 调用时机 |
|------|----------|
| `pre_start` | 策略启动前，用于初始化和订阅 |
| `post_start` | 策略启动后，所有组件已就绪 |
| `on_quote` | 收到行情数据时 |
| `on_order` | 收到订单状态更新时 |
| `on_trade` | 收到成交回报时 |
| `on_broker_state_change` | Broker 状态变化时 |

---

## 数据流详解

### Journal 写入路径

| 进程 | 写入 | Journal 文件路径 |
|------|------|-----------------|
| MD | Quote (广播) | `journal/md/sim/sim/live/00000000.*.journal` |
| Strategy | OrderInput (定向写给 TD) | `journal/strategy/default/demo/live/{td_uid_hex}.*.journal` |
| TD | Order/Trade (广播) | `journal/td/sim/sim/live/00000000.*.journal` |

### 通道建立协议

1. Master 启动，bind NNG PUB + PULL
2. MD 注册 → Master 记录
3. TD 注册 → Master 记录
4. Strategy 注册 → `add_md("sim","sim")` → `request_write_to(md_uid)` + `request_read_from(md_loc, 0)` → Reader join MD 公开 journal
5. Strategy → `add_account("sim","sim")` → `request_write_to(td_uid)` → Master 发布 `Channel(strat_uid, td_uid)` via NNG PUB
6. TD 收到 Channel → `reader_.join(strategy_loc, home_uid())` → 开始读取策略写给它的 OrderInput
7. Strategy → `request_read_from(td_loc, 0)` → Reader join TD 公开 journal → 收到 Order/Trade

---

## 实现状态

| 阶段 | 模块 | 状态 |
|------|------|------|
| Phase 1 | Journal mmap IPC (Page/Frame/Reader/Writer) | Done |
| Phase 1 | IO Locator (路径定位、UID 生成) | Done |
| Phase 1 | Longfist 数据类型 + Hana 反射 | Done |
| Phase 1 | NNG Socket 封装 | Done |
| Phase 1 | Practice (hero/apprentice 事件循环) | Done |
| Phase 2 | Master (注册/Channel/NNG 协调) | Done |
| Phase 2 | Ledger (BookKeeper/持仓/PnL) | Done |
| Phase 2 | Cached (状态恢复) | Done |
| Phase 3 | BrokerVendor/Service 抽象 | Done |
| Phase 3 | SimMD (随机漫步行情生成) | Done |
| Phase 3 | SimTD + MatchingEngine (限价撮合) | Done |
| Phase 3 | Strategy Runner + RuntimeContext | Done |
| Phase 3 | 多进程 SIM (Master/MD/TD/Strategy) | Done |
| Phase 4 | 真实交易所扩展 (XTP/CTP) | Not started |
| Phase 4 | API Gateway (REST/WebSocket) | In progress |
| Phase 5 | Qt UI | Not started |

---

## 与 kungfu-origin 的差异

| 方面 | 原项目 | 本项目 |
|------|--------|--------|
| 进程管理 | PM2 (Node.js) | 外部工具 (systemd/脚本) |
| UI | Electron + Vue | 未实现 (计划 Qt) |
| 策略语言 | Python / C++ | 纯 C++ |
| 构建系统 | cmake-js + Conan + Yarn | CMake |
| 技术栈 | C++ / TS / Python / Vue | 纯 C++20 |
| 序列化 | Boost.Hana | Boost.Hana (保持) |
| 消息通信 | nng | nng (保持) |