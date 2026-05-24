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
│ kf_md_sim  │──── Quote ───────►│kf_strategy │──── OrderInput ──►│ kf_td_sim  │
└────────────┘                   └────────────┘                   └────────────┘
                                       ▲                                │
                                       └─────── Order/Trade ────────────┘
                                                Journal mmap
```

### 进程职责

| 进程 | 说明 |
|------|------|
| `kf_master` | 系统协调器：进程注册、Channel 通道建立、NNG PUB/PULL |
| `kf_md_sim` | SIM 行情源：随机漫步生成 Quote，写入公开 Journal |
| `kf_td_sim` | SIM 交易所：读取 OrderInput，撮合后写回 Order/Trade |
| `kf_strategy` | 策略进程：订阅行情、下单、接收回报 |

辅助服务（已实现，多进程 SIM 不需要）：

| 进程 | 说明 |
|------|------|
| `kf_ledger` | 聚合持仓/资产（多策略风控场景需要） |
| `kf_cached` | 状态恢复服务 |
| `kf_archive` | Journal 归档清理 |

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
│   ├── googletest/        # 测试框架
│   ├── hana/              # Boost.Hana 编译期反射
│   ├── nlohmann/          # JSON
│   ├── nng/               # NNG (进程间控制)
│   ├── rxcpp/             # 响应式事件循环
│   ├── spdlog/            # 日志
│   ├── sqlite3/           # SQLite
│   ├── sqlite_orm/        # ORM
│   └── tomlplusplus/      # TOML 配置
├── include/kungfu/        # 公共头文件
│   ├── common/            # 配置、hash、mmap
│   ├── longfist/          # 数据类型、枚举、序列化
│   ├── service/           # Master/Ledger/Cached/Archive
│   ├── wingchun/          # 策略引擎、Broker、Gateway
│   └── yijinjing/         # Journal、Locator、Practice
├── src/
│   ├── common/            # 配置加载、hash、mmap 实现
│   ├── yijinjing/         # Journal 系统、NNG socket、Practice
│   ├── wingchun/          # 交易引擎核心
│   │   ├── broker/        # BrokerVendor、BrokerClient
│   │   ├── gateway/sim/   # SimMD、SimTD、MatchingEngine、main_md/td
│   │   └── strategy/      # Runner、RuntimeContext、SimContext
│   └── services/          # 独立服务进程
│       ├── master/        # kf_master
│       ├── ledger/        # kf_ledger (含 BookKeeper)
│       ├── cached/        # kf_cached
│       └── archive/       # kf_archive
├── examples/
│   ├── multi_process_strategy.cpp   # 多进程策略 (kf_strategy)
│   └── sim_strategy_demo.cpp        # 单进程 SIM 演示 (kf_strategy_sim)
├── tests/                 # 88 个测试用例
├── kungfu.toml            # 配置文件
└── CMakeLists.txt
```

---

## 编译

```bash
# 前置要求: CMake 3.20+, C++20 编译器 (MSVC 2022 / GCC 11+ / Clang 14+)

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# 运行测试 (88 个)
ctest --build-config Release
```

### 编译产物

| 目标 | 路径 | 说明 |
|------|------|------|
| `kf_master` | `src/services/master/Release/` | Master 协调进程 |
| `kf_md_sim` | `src/wingchun/Release/` | SIM 行情进程 |
| `kf_td_sim` | `src/wingchun/Release/` | SIM 交易进程 |
| `kf_strategy` | `examples/Release/` | 多进程策略示例 |
| `kf_strategy_sim` | `examples/Release/` | 单进程 SIM 演示 |
| `kf_ledger` | `src/services/ledger/Release/` | 账本服务 |
| `kf_cached` | `src/services/cached/Release/` | 缓存服务 |
| `kf_archive` | `src/services/archive/Release/` | 归档服务 |

---

## 运行多进程 SIM

### 配置文件 (kungfu.toml)

```toml
[system]
home = "/path/to/kungfu/home"
log_level = "info"
low_latency = false
page_size = 1048576
```

### 启动顺序

```bash
# 终端1: Master (必须首先启动)
./kf_master kungfu.toml

# 终端2: SIM 行情
./kf_md_sim kungfu.toml

# 终端3: SIM 交易
./kf_td_sim kungfu.toml

# 终端4: 策略
./kf_strategy kungfu.toml
```

### 预期输出

**Master:**
```
Master: PUB bound to ipc:///kungfu/system/master/master/live/pub
Master: PULL bound to ipc:///kungfu/system/master/master/live/pull
Master: registered app uid=3243096914 name=sim/sim pid=...
Master: registered app uid=2057459272 name=sim/sim pid=...
Master: registered app uid=2249264571 name=default/demo pid=...
Master: published channel source=2249264571 dest=2057459272
```

**Strategy:**
```
[Strategy] pre_start: subscribed 600000@SSE, waiting for quotes...
[Strategy] on_quote #1: 600000 last=10.0178 bid=10.0078 ask=10.0278
[Strategy] placed BUY order: id=1 price=10.0256 vol=100
[Strategy] on_order: id=1 status=1 traded=0/100
[Strategy] on_trade: id=1 price=9.9640 vol=100
[Strategy] position: vol=100 avg_price=9.9640
[Strategy] on_order: id=1 status=5 traded=0/0
```

**TD_sim:**
```
TraderVendor: joined reader for source_uid=2249264571 dest=2057459272
TraderVendor: received OrderInput id=1 600000 @ SSE
```

---

## 单进程 SIM 演示

不需要 Master/MD/TD 进程，所有组件在一个进程内直接调用：

```bash
./kf_strategy_sim kungfu.toml
```

输出包含完整交易循环：开仓 → 持仓跟踪 → 平仓 → 实现盈亏。

---

## 策略开发示例

```cpp
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/wingchun/strategy/runner.h>
#include <kungfu/wingchun/strategy/runtime_context.h>

using namespace kungfu::wingchun::strategy;
using namespace kungfu::longfist;

class MyStrategy : public Strategy {
public:
    void pre_start(Context& ctx) override {
        ctx.add_md("sim", "sim");
        ctx.add_account("sim", "sim");
        ctx.subscribe("SSE", "600000");
    }

    void on_quote(Context& ctx, const types::Quote& quote) override {
        if (!ordered_ && quote.ask_price_0 > 0) {
            ctx.insert_order("600000", "SSE", quote.ask_price_0, 100,
                           enums::Side::Buy, enums::Offset::Open,
                           enums::PriceType::Limit);
            ordered_ = true;
        }
    }

    void on_trade(Context& ctx, const types::Trade& trade) override {
        auto pos = ctx.get_position("600000", "SSE", enums::Direction::Long);
        if (pos) { /* position tracking */ }
    }

private:
    bool ordered_ = false;
};
```

---

## 数据流详解

### Journal 写入路径

| 进程 | 写入 | Journal 文件路径 |
|------|------|-----------------|
| MD_sim | Quote (广播) | `journal/md/sim/sim/live/00000000.*.journal` |
| Strategy | OrderInput (定向写给 TD) | `journal/strategy/default/demo/live/{td_uid_hex}.*.journal` |
| TD_sim | Order/Trade (广播) | `journal/td/sim/sim/live/00000000.*.journal` |

### 通道建立协议

1. Master 启动，bind NNG PUB + PULL
2. MD_sim 注册 → Master 记录
3. TD_sim 注册 → Master 记录
4. Strategy 注册 → `add_md("sim","sim")` → `request_write_to(md_uid)` + `request_read_from(md_loc, 0)` → Reader join MD 公开 journal
5. Strategy → `add_account("sim","sim")` → `request_write_to(td_uid)` → Master 发布 `Channel(strat_uid, td_uid)` via NNG PUB
6. TD_sim 收到 Channel → `reader_.join(strategy_loc, home_uid())` → 开始读取策略写给它的 OrderInput
7. Strategy → `request_read_from(td_loc, 0)` → Reader join TD 公开 journal → 收到 Order/Trade

---

## 测试

```bash
ctest --build-config Release          # 全部 88 个测试
ctest --build-config Release -R E2E   # E2E 交易测试
ctest --build-config Release -R Journal  # Journal 读写测试
```

测试覆盖：
- Journal mmap 读写、Page 滚动
- FrameHeader 序列化/反序列化
- Locator 路径生成、UID hash
- MatchingEngine 撮合逻辑（全额/部分成交）
- BookKeeper 持仓计算、PnL 更新
- Strategy/Context 集成
- E2E 交易流程（Quote → Order → Trade → Position）

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
| Phase 2 | Archive (Journal 清理) | Done |
| Phase 3 | BrokerVendor/Service 抽象 | Done |
| Phase 3 | SimMD (随机漫步行情生成) | Done |
| Phase 3 | SimTD + MatchingEngine (限价撮合) | Done |
| Phase 3 | Strategy Runner + RuntimeContext | Done |
| Phase 3 | 多进程 SIM (Master/MD/TD/Strategy) | Done |
| Phase 4 | 真实交易所扩展 (XTP/CTP) | Not started |
| Phase 4 | API Gateway (REST/WebSocket) | Not started |
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
