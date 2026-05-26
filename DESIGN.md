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
| **纯 C++** | 去除 Node.js/Python/Electron 依赖，统一技术栈 |

### 1.2 架构演进

```
┌─────────────────────────────────────────────────────────────────┐
│                    原架构 (kungfu-origin)                       │
│  Electron UI + PM2 进程管理 + C++ Core + Python 策略扩展        │
│  技术栈: C++ / TypeScript / Python / Vue / Node.js              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    新架构 (kungfu-cpp)                          │
│  Qt UI (独立子项目) + RESTful/WS API + C++20 Core              │
│  技术栈: 纯 C++20 + Boost + Qt                                 │
│  进程管理: Master 内置 Supervisor（类 systemd）                 │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 与原项目的主要差异

| 方面 | 原项目 | 新项目 |
|------|--------|--------|
| 进程管理 | PM2 (Node.js) | Master 内置 Supervisor |
| UI 框架 | Electron + Vue | Qt（独立子项目） |
| 策略语言 | Python / C++ | C++（动态库） |
| REST API | Node.js | Boost.Beast |
| 序列化 | Boost.Hana 编译期反射 | Boost.Hana 编译期反射（保持） |
| 消息通信 | nng (nanomsg-next-gen) | nng (nanomsg-next-gen) |
| 构建系统 | cmake-js + Conan + Yarn | CMake + Conan |

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
│  │  │Boost.Beast│  │ WebSocket │  │  认证鉴权  │  │  日志审计  │   │    │
│  │  │HTTP Server│  │   Server  │  │  (JWT)    │  │           │   │    │
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘   │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ Yijinjing (共享内存 Journal)
                                      ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                              核心层                                   │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │              Master (系统协调 + 进程 Supervisor)                 │    │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐   │    │
│  │  │ 进程注册   │  │ 通道协调   │  │ 时间同步   │  │ 进程监控   │   │    │
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘   │    │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐                   │    │
│  │  │ 自动重启   │  │ 依赖排序   │  │ 优雅停止   │                   │    │
│  │  └───────────┘  └───────────┘  └───────────┘                   │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐            │
│  │  Ledger   │  │  Cached   │  │  Archive  │  │   API     │            │
│  │  (账本)    │  │ (状态恢复) │  │  (归档)    │  │ (网关)    │            │
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
| **master** | 系统核心协调器：进程管理（Supervisor）、通道管理、时间同步 | `master` |
| **ledger** | 账本服务：交易数据持久化、持仓管理、手续费计算 | `ledger` |
| **cached** | 状态恢复服务：启动时从 Journal 重放恢复系统状态 | `cached` |
| **archive** | 归档服务：定期清理过期 Journal 文件 | `archive` |
| **md** | 行情数据服务：行情订阅与分发 | `md` |
| **td** | 交易服务：订单管理、自成交检测 | `td` |
| **strategy** | 策略引擎：策略生命周期管理、事件回调 | `strategy` |
| **api** | API 网关：RESTful/WebSocket 接口 | `kf-api` |

### 2.3 进程间通信架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                        通信机制架构                                 │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                  Yijinjing (核心通信层)                      │   │
│  │                                                             │   │
│  │  ┌──────────────────────────────────────────────────────┐   │   │
│  │  │  Journal (共享内存 mmap)                             │   │   │
│  │  │  ┌─────────┐  ┌─────────┐  ┌─────────┐             │   │   │
│  │  │  │  Page   │→ │  Frame  │→ │  Data   │             │   │   │
│  │  │  │ (文件)   │  │ (帧头)   │  │ (载荷)   │             │   │   │
│  │  │  └─────────┘  └─────────┘  └─────────┘             │   │   │
│  │  └──────────────────────────────────────────────────────┘   │   │
│  │                                                             │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐   │   │
│  │  │   Channel   │  │  IO Locator │  │      nng        │   │   │
│  │  │  (通道管理)  │  │ (路径定位)   │  │  (控制消息)      │   │   │
│  │  └─────────────┘  └─────────────┘  └─────────────────┘   │   │
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
├── 3rdparty/                    # 第三方依赖 (git submodule)
│   ├── boost/                  # Boost (Beast/Hana/Asio)
│   ├── nng/                    # 进程间控制消息 (nanomsg-next-gen)
│   ├── nlohmann/               # JSON 解析 (header-only)
│   ├── spdlog/                 # 日志库
│   ├── sqlite3/                # 嵌入式数据库 (amalgamation)
│   ├── sqlite_orm/             # SQLite ORM (header-only)
│   ├── tomlplusplus/           # TOML 配置解析 (header-only)
│   ├── rxcpp/                  # 响应式编程库 (header-only)
│   └── googletest/             # 测试框架
├── src/                        # 核心源码
│   ├── core/                   # 核心框架
│   │   ├── yijinjing/          # 事件总线框架
│   │   ├── longfist/           # 数据类型定义 + Boost.Hana 序列化
│   │   └── wingchun/           # 策略引擎核心
│   ├── services/               # 服务模块
│   │   ├── master/             # Master 服务 (含 Supervisor)
│   │   ├── ledger/             # Ledger 服务
│   │   ├── cached/             # Cached 服务
│   │   ├── archive/            # Archive 服务
│   │   ├── md/                 # 行情服务
│   │   ├── td/                 # 交易服务
│   │   ├── strategy/           # 策略引擎
│   │   └── api/                # API 网关 (Boost.Beast)
│   ├── tools/                  # 工具模块
│   │   ├── status/             # 服务状态查看工具
│   │   ├── log/                # 日志查看工具
│   │   └── config/             # 配置管理工具
│   ├── common/                 # 公共组件
│   │   ├── utils/              # 工具函数
│   │   ├── logging/            # 日志系统 (spdlog 封装)
│   │   └── config/             # 配置管理 (TOML)
│   └── extensions/             # 扩展模块
│       ├── xtp/                # XTP 交易所接口
│       ├── ctp/                # CTP 交易所接口
│       └── sim/                # 模拟交易接口
├── include/                    # 公共头文件
│   └── kungfu/
│       ├── yijinjing/          # 事件总线框架接口
│       ├── longfist/           # 数据类型定义接口
│       ├── wingchun/           # 策略引擎核心接口
│       └── common/             # 公共组件接口
├── config/                     # 配置文件
│   └── kungfu.toml             # 统一配置文件
├── tests/                      # 测试代码
│   ├── unit/                   # 单元测试
│   ├── integration/            # 集成测试
│   └── performance/            # 性能测试（延迟基准测试）
├── scripts/                    # 脚本文件
│   ├── build.sh                # 编译脚本
│   └── deploy.sh              # 部署脚本
├── .gitignore
├── .gitmodules                 # git submodule 配置
├── CMakeLists.txt              # 主 CMake 配置
├── ChangeLog.md
└── README.md
```

### 3.2 目录职责说明

| 目录 | 职责 | 说明 |
|------|------|------|
| `3rdparty/` | 第三方依赖 | git submodule 管理：boost、nng、nlohmann、spdlog、sqlite3、sqlite_orm、tomlplusplus、rxcpp、googletest |
| `src/core/yijinjing/` | 事件总线 | Journal 共享内存、Page/Frame 管理、IO Locator、Practice 层 |
| `src/core/longfist/` | 数据类型 | 类型定义、Boost.Hana 编译期反射、自动序列化 |
| `src/core/wingchun/` | 策略核心 | 策略运行时、Broker 抽象、Book 管理 |
| `src/services/` | 服务模块 | master、ledger、cached、archive、md、td、strategy、api |
| `src/tools/` | 运维工具 | 服务状态查看、日志查看、配置管理 |
| `src/common/` | 公共组件 | 工具函数、日志封装、配置管理 |
| `src/extensions/` | 交易所扩展 | 编译为平台相关动态库（.so / .dll / .dylib） |
| `include/kungfu/` | 公共头文件 | 对外暴露的接口定义 |
| `tests/` | 测试代码 | 单元、集成、性能（含延迟基准测试） |

---

## 四、核心框架设计

### 4.1 Yijinjing（易筋经）— 事件总线框架

#### 4.1.1 整体结构

```
yijinjing/
├── journal/                    # Journal 共享内存系统
│   ├── page.hpp               # 内存映射文件页
│   ├── frame.hpp              # 帧（消息单元）
│   ├── reader.hpp             # 读取器
│   └── writer.hpp             # 写入器
├── io/                        # IO 层
│   ├── locator.hpp            # 路径定位器
│   ├── sqlite_store.hpp       # SQLite 存储
│   └── trace.hpp              # 追踪/诊断
├── cache/                     # 缓存层
│   ├── ring_queue.hpp         # 无锁环形队列
│   └── cached_store.hpp       # 缓存存储
├── practice/                  # 实践层（进程协作模型）
│   ├── hero.hpp               # Master 基类
│   ├── apprentice.hpp         # 子服务基类
│   └── profile.hpp            # 配置管理
├── time/                      # 时间系统
│   ├── timer.hpp              # 高精度定时器
│   └── time_utils.hpp         # 时间工具（纳秒精度）
└── util/                      # 工具
    ├── mmap.hpp               # 平台抽象 mmap
    ├── signal.hpp             # 信号处理
    └── nanomsg.hpp            # nng 封装
```

#### 4.1.2 Journal 系统设计

Journal 是核心数据通道，基于内存映射文件（mmap）实现零拷贝进程间通信：

```
┌─────────────────────────────────────────────────────────────────┐
│                    Journal 文件结构                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Page (内存映射文件, 固定大小 128MB)                            │
│  ┌──────────┬──────────┬──────────┬─────────────────────────┐  │
│  │ Frame 0  │ Frame 1  │ Frame 2  │ ...                     │  │
│  └──────────┴──────────┴──────────┴─────────────────────────┘  │
│                                                                 │
│  Frame (单条消息)                                               │
│  ┌──────────────────────────────────────────────┐              │
│  │ Header (24 bytes)                            │              │
│  │  ┌────────┬────────┬────────┬──────────────┐ │              │
│  │  │ length │msg_type│  time  │  source_uid  │ │              │
│  │  │ 4B     │ 2B     │ 8B     │  4B          │ │              │
│  │  └────────┴────────┴────────┴──────────────┘ │              │
│  ├──────────────────────────────────────────────┤              │
│  │ Data (变长, 由 msg_type 决定)                │              │
│  └──────────────────────────────────────────────┘              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

```cpp
// journal/frame.hpp
namespace kungfu::yijinjing::journal {

struct FrameHeader {
    uint32_t length;       // 帧总长度
    uint16_t msg_type;     // 消息类型（对应 longfist 数据类型）
    uint16_t source;       // 来源标识
    int64_t  gen_time;     // 生成时间（纳秒）
    uint32_t source_uid;   // 来源进程 UID
    uint32_t dest_uid;     // 目标进程 UID (0=广播)
};

class Frame {
public:
    const FrameHeader& header() const;
    template<typename T> const T& data() const;
    int64_t gen_time() const;
    uint16_t msg_type() const;
};

} // namespace kungfu::yijinjing::journal
```

```cpp
// journal/page.hpp
namespace kungfu::yijinjing::journal {

class Page {
public:
    static std::shared_ptr<Page> open(const std::string& path, bool is_writing);
    
    Frame& current_frame();
    void next_frame();
    bool is_full() const;
    
private:
    void* mmap_addr_;      // mmap 映射地址
    size_t page_size_;     // 页大小 (128MB)
    size_t position_;      // 当前偏移
};

} // namespace kungfu::yijinjing::journal
```

```cpp
// journal/writer.hpp
namespace kungfu::yijinjing::journal {

class Writer {
public:
    Writer(const io::location_ptr& location, uint32_t dest_uid, bool lazy);
    
    template<typename T>
    void write(int64_t trigger_time, const T& data);
    
    void mark(int64_t trigger_time, uint16_t msg_type);
    
private:
    Page& current_page();
    void roll_page();  // 当前 Page 写满时滚动到新 Page
    
    uint32_t dest_uid_;
    std::vector<std::shared_ptr<Page>> pages_;
};

} // namespace kungfu::yijinjing::journal
```

```cpp
// journal/reader.hpp
namespace kungfu::yijinjing::journal {

class Reader {
public:
    Reader(bool lazy);
    
    void join(const io::location_ptr& location, uint32_t dest_uid, int64_t from_time);
    bool data_available();
    Frame& current_frame();
    void next();
    void seek_to_time(int64_t nanotime);
    
private:
    struct JournalEntry {
        io::location_ptr location;
        uint32_t dest_uid;
        std::shared_ptr<Page> current_page;
        size_t position;
    };
    std::vector<JournalEntry> journals_;
    int current_journal_idx_;
};

} // namespace kungfu::yijinjing::journal
```

#### 4.1.3 IO Locator（路径定位器）

IO Locator 定义了 Journal 文件的存储路径规则，通过 `category/group/name/mode` 四维定位：

```cpp
// io/locator.hpp
namespace kungfu::yijinjing::io {

enum class category : uint8_t {
    system,     // 系统级（master、archive）
    md,         // 行情数据
    td,         // 交易数据
    strategy,   // 策略
    service     // 服务（ledger、cached、api）
};

enum class mode : uint8_t {
    live,       // 实盘
    data,       // 数据
    replay,     // 回放
    backtest    // 回测
};

struct location {
    category category;
    std::string group;       // 例如: "xtp", "ctp"
    std::string name;        // 例如: "account001"
    mode mode;
    uint32_t uid;            // 唯一标识 (hash)
    
    std::string journal_path(const Locator& locator, uint32_t dest_uid) const;
};

using location_ptr = std::shared_ptr<location>;

class Locator {
public:
    Locator(const std::string& root_path);
    
    std::string layout_dir(const location_ptr& location) const;
    std::string layout_file(const location_ptr& location, const std::string& filename) const;
    std::vector<uint32_t> list_page_id(const location_ptr& location, uint32_t dest_uid) const;
    
private:
    std::string root_path_;
};

} // namespace kungfu::yijinjing::io
```

Journal 文件路径示例：
```
{root}/journal/{category}/{group}/{name}/{mode}/{dest_uid}.{page_id}.journal
例如:
/var/kungfu/journal/md/xtp/xtp/live/0.1.journal
/var/kungfu/journal/td/xtp/account001/live/ledger_uid.1.journal
```

#### 4.1.4 Practice 层（进程协作模型）

Practice 层定义了进程间协作的基类体系，基于 rxcpp 实现事件驱动：

```cpp
// practice/hero.hpp (Master 专用基类)
namespace kungfu::yijinjing::practice {

class hero {
public:
    hero(const io::Locator& locator, io::mode mode, bool low_latency);
    virtual ~hero() = default;
    
    void run();
    void stop();
    
    virtual void react() = 0;
    virtual void on_exit() {}
    
protected:
    // 事件循环 (rxcpp)
    rxcpp::observable<event_ptr> events_;
    rxcpp::composite_subscription lifetime_;
    
    // 读写器
    journal::Reader reader_;
    std::unordered_map<uint32_t, std::shared_ptr<journal::Writer>> writers_;
    
    io::location_ptr get_location(uint32_t uid) const;
    std::shared_ptr<journal::Writer> get_writer(uint32_t dest_uid);
    
    int64_t now() const;  // 纳秒时间戳
    bool low_latency_;
};

} // namespace kungfu::yijinjing::practice
```

```cpp
// practice/apprentice.hpp (子服务基类)
namespace kungfu::yijinjing::practice {

class apprentice {
public:
    apprentice(const io::location_ptr& home, bool low_latency);
    virtual ~apprentice() = default;
    
    void run();
    void stop();
    
    virtual void react() = 0;
    virtual void on_start() {}
    virtual void on_exit() {}
    
    // 向 Master 注册
    void register_self();
    
    // 请求对某个 location 建立写通道
    void request_write_to(uint32_t dest_uid);
    void request_read_from(uint32_t source_uid, int64_t from_time);
    
protected:
    rxcpp::observable<event_ptr> events_;
    rxcpp::composite_subscription lifetime_;
    
    journal::Reader reader_;
    std::unordered_map<uint32_t, std::shared_ptr<journal::Writer>> writers_;
    
    io::location_ptr home_;
    int64_t now() const;
    bool low_latency_;
};

} // namespace kungfu::yijinjing::practice
```

#### 4.1.5 事件循环模型

基于 rxcpp 的响应式事件驱动：

```cpp
// 事件处理示例
void Ledger::react() {
    events_ | rx::filter([](const event_ptr& e) {
        return e->msg_type() == longfist::types::Order::tag;
    }) | rx::subscribe<event_ptr>(lifetime_, [this](const event_ptr& event) {
        on_order(event);
    });
    
    events_ | rx::filter([](const event_ptr& e) {
        return e->msg_type() == longfist::types::Trade::tag;
    }) | rx::subscribe<event_ptr>(lifetime_, [this](const event_ptr& event) {
        on_trade(event);
    });
}
```

### 4.2 Longfist（龙拳）— 数据类型与序列化

#### 4.2.1 设计原理

使用 Boost.Hana 实现编译期反射，让数据结构自动获得：
- 序列化到 Journal Frame（二进制，零拷贝）
- 序列化到 JSON（API 交互）
- 映射到 SQLite 表（持久化）

```cpp
// longfist/types.h
#include <boost/hana.hpp>

namespace kungfu::longfist::types {

namespace hana = boost::hana;

// 使用 BOOST_HANA_DEFINE_STRUCT 实现编译期反射
struct Quote {
    BOOST_HANA_DEFINE_STRUCT(Quote,
        (char[32],   instrument_id),
        (char[16],   exchange_id),
        (int64_t,    data_time),
        (double,     last_price),
        (double,     pre_close_price),
        (double,     open_price),
        (double,     high_price),
        (double,     low_price),
        (int64_t,    volume),
        (double,     turnover),
        (double,     bid_price[5]),
        (int64_t,    bid_volume[5]),
        (double,     ask_price[5]),
        (int64_t,    ask_volume[5])
    );
    
    static constexpr uint16_t tag = 101;
};

struct OrderInput {
    BOOST_HANA_DEFINE_STRUCT(OrderInput,
        (uint64_t,   order_id),
        (char[32],   instrument_id),
        (char[16],   exchange_id),
        (double,     limit_price),
        (int64_t,    volume),
        (Side,       side),
        (Offset,     offset),
        (PriceType,  price_type)
    );
    
    static constexpr uint16_t tag = 201;
};

struct Order {
    BOOST_HANA_DEFINE_STRUCT(Order,
        (uint64_t,    order_id),
        (char[32],    instrument_id),
        (char[16],    exchange_id),
        (double,      limit_price),
        (double,      frozen_price),
        (int64_t,     volume),
        (int64_t,     volume_traded),
        (int64_t,     volume_left),
        (OrderStatus, status),
        (Side,        side),
        (Offset,      offset),
        (int64_t,     insert_time),
        (int64_t,     update_time),
        (char[128],   error_msg)
    );
    
    static constexpr uint16_t tag = 202;
};

struct Trade {
    BOOST_HANA_DEFINE_STRUCT(Trade,
        (uint64_t,  trade_id),
        (uint64_t,  order_id),
        (char[32],  instrument_id),
        (char[16],  exchange_id),
        (double,    price),
        (int64_t,   volume),
        (Side,      side),
        (Offset,    offset),
        (int64_t,   trade_time)
    );
    
    static constexpr uint16_t tag = 203;
};

struct Position {
    BOOST_HANA_DEFINE_STRUCT(Position,
        (char[32],   instrument_id),
        (char[16],   exchange_id),
        (Direction,  direction),
        (int64_t,    volume),
        (int64_t,    yesterday_volume),
        (double,     avg_open_price),
        (double,     position_cost),
        (double,     unrealized_pnl),
        (double,     realized_pnl)
    );
    
    static constexpr uint16_t tag = 301;
};

struct Asset {
    BOOST_HANA_DEFINE_STRUCT(Asset,
        (char[32],  account_id),
        (double,    initial_equity),
        (double,    static_equity),
        (double,    dynamic_equity),
        (double,    available),
        (double,    margin),
        (double,    frozen_cash),
        (double,    frozen_margin),
        (double,    frozen_fee),
        (double,    realized_pnl),
        (double,    unrealized_pnl)
    );
    
    static constexpr uint16_t tag = 302;
};

struct Bar {
    BOOST_HANA_DEFINE_STRUCT(Bar,
        (char[32],   instrument_id),
        (char[16],   exchange_id),
        (int64_t,    start_time),
        (int64_t,    end_time),
        (double,     open),
        (double,     high),
        (double,     low),
        (double,     close),
        (int64_t,    volume),
        (double,     turnover)
    );
    
    static constexpr uint16_t tag = 102;
};

struct Instrument {
    BOOST_HANA_DEFINE_STRUCT(Instrument,
        (char[32],        instrument_id),
        (char[16],        exchange_id),
        (InstrumentType,  instrument_type),
        (double,          price_tick),
        (int32_t,         delivery_year),
        (int32_t,         delivery_month),
        (int32_t,         contract_multiplier),
        (double,          long_margin_ratio),
        (double,          short_margin_ratio)
    );
    
    static constexpr uint16_t tag = 103;
};

} // namespace kungfu::longfist::types
```

#### 4.2.2 自动序列化实现

```cpp
// longfist/serialize.hpp
namespace kungfu::longfist {

// 自动 JSON 序列化（基于 Boost.Hana 遍历成员）
template<typename T>
nlohmann::json to_json(const T& obj) {
    nlohmann::json j;
    hana::for_each(hana::members(obj), [&](auto pair) {
        auto key = hana::to<const char*>(hana::first(pair));
        j[key] = hana::second(pair);
    });
    return j;
}

template<typename T>
T from_json(const nlohmann::json& j) {
    T obj{};
    hana::for_each(hana::keys(obj), [&](auto key) {
        auto name = hana::to<const char*>(key);
        if (j.contains(name)) {
            hana::at_key(obj, key) = j[name].get<decltype(hana::at_key(obj, key))>();
        }
    });
    return obj;
}

// 自动 SQLite 表创建（基于 Boost.Hana 遍历成员名和类型）
template<typename T>
std::string create_table_sql(const std::string& table_name) {
    std::string sql = "CREATE TABLE IF NOT EXISTS " + table_name + " (";
    bool first = true;
    hana::for_each(hana::accessors<T>(), [&](auto accessor) {
        if (!first) sql += ", ";
        first = false;
        sql += hana::to<const char*>(hana::first(accessor));
        sql += " " + sqlite_type_name<decltype(hana::second(accessor)(std::declval<T>()))>();
    });
    sql += ")";
    return sql;
}

} // namespace kungfu::longfist
```

#### 4.2.3 枚举类型定义

```cpp
// longfist/enums.h
namespace kungfu::longfist::enums {

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class Offset : uint8_t { Open = 0, Close = 1, CloseToday = 2, CloseYesterday = 3 };
enum class Direction : uint8_t { Long = 0, Short = 1 };
enum class PriceType : uint8_t { Limit = 0, Market = 1, BestPrice = 2, FakBest5 = 3 };
enum class OrderStatus : uint8_t {
    Unknown = 0, Submitted = 1, Pending = 2, Cancelled = 3,
    Error = 4, Filled = 5, PartialFilledNotActive = 6, PartialFilledActive = 7
};
enum class InstrumentType : uint8_t {
    Unknown = 0, Stock = 1, Future = 2, Bond = 3,
    StockOption = 4, Fund = 5, Index = 6, Repo = 7, Crypto = 8
};
enum class BrokerState : uint8_t {
    Unknown = 0, Idle = 1, DisConnected = 2, Connected = 3,
    LoggedIn = 4, Ready = 5, LoginFailed = 6
};

} // namespace kungfu::longfist::enums
```

---

## 五、服务模块设计

### 5.1 Master 模块（含进程 Supervisor）

#### 5.1.1 核心职责

| 职责 | 说明 |
|------|------|
| **进程 Supervisor** | 启动、监控、自动重启子进程（类似 systemd） |
| **进程注册管理** | 接收并管理所有子进程的注册请求 |
| **通道协调** | 建立进程间的通信通道 |
| **时间同步** | 发布交易日信息和时间基准 |
| **依赖排序** | 按依赖关系有序启动服务 |
| **优雅停止** | 按逆序停止所有子进程 |

#### 5.1.2 Supervisor 设计

```cpp
// master/supervisor.hpp
namespace kungfu::master {

enum class ProcessState : uint8_t {
    Stopped,      // 未启动
    Starting,     // 启动中
    Running,      // 正常运行
    Stopping,     // 停止中
    Failed,       // 启动失败
    Exited        // 异常退出
};

enum class RestartPolicy : uint8_t {
    Always,       // 总是重启
    OnFailure,    // 仅异常退出时重启
    Never         // 不重启
};

struct ProcessConfig {
    std::string name;                   // 进程名称
    std::string executable;             // 可执行文件路径
    std::vector<std::string> args;      // 命令行参数
    RestartPolicy restart_policy;       // 重启策略
    int max_restart_count;              // 最大重启次数（窗口内）
    int restart_window_seconds;         // 重启计数窗口
    int restart_delay_ms;               // 重启延迟（毫秒）
    int startup_timeout_ms;             // 启动超时
    std::vector<std::string> depends_on;// 依赖的服务名
    int priority;                       // 启动优先级 (0=最高)
};

struct ProcessInfo {
    ProcessConfig config;
    ProcessState state;
    int pid;
    int restart_count;
    int64_t start_time;
    int64_t last_exit_time;
    int last_exit_code;
};

class Supervisor {
public:
    Supervisor(const std::vector<ProcessConfig>& configs);
    
    // 启动所有服务（按依赖拓扑排序）
    void start_all();
    
    // 停止所有服务（按逆序）
    void stop_all(int timeout_ms = 5000);
    
    // 单个进程控制
    void start_process(const std::string& name);
    void stop_process(const std::string& name, int timeout_ms = 5000);
    void restart_process(const std::string& name);
    
    // 状态查询
    ProcessInfo get_process_info(const std::string& name) const;
    std::vector<ProcessInfo> get_all_info() const;
    
    // 监控循环（定期检查子进程状态）
    void monitor_loop();
    
private:
    void do_start(const std::string& name);
    void do_stop(const std::string& name, int timeout_ms);
    void handle_child_exit(const std::string& name, int exit_code);
    bool should_restart(const ProcessInfo& info) const;
    std::vector<std::string> topological_sort() const;
    
    // 平台抽象
    int spawn_process(const ProcessConfig& config);
    void kill_process(int pid, bool force);
    bool is_process_alive(int pid) const;
    
    std::unordered_map<std::string, ProcessInfo> processes_;
    std::mutex mutex_;
};

} // namespace kungfu::master
```

#### 5.1.3 Master 主类

```cpp
// master/master.hpp
namespace kungfu::master {

class Master : public yijinjing::practice::hero {
public:
    Master(const io::Locator& locator, bool low_latency);
    
    void react() override;
    
    // 进程管理
    void register_app(const event_ptr& event);
    void deregister_app(int64_t trigger_time, uint32_t app_uid);
    
    // 通道管理
    void on_channel_request(const event_ptr& event);
    void require_write_to(int64_t time, uint32_t source_uid, uint32_t dest_uid);
    
    // 时间同步
    void publish_trading_day();
    void write_time_reset(int64_t time, std::shared_ptr<journal::Writer> writer);
    
protected:
    void on_exit() override;
    
private:
    Supervisor supervisor_;
    std::unordered_map<uint32_t, std::shared_ptr<journal::Writer>> writers_;
    std::unordered_map<uint32_t, io::location_ptr> locations_;
};

} // namespace kungfu::master
```

#### 5.1.4 启动流程

```
Master 启动流程:
1. 加载配置文件 (kungfu.toml)
2. 初始化 Journal 系统
3. 初始化 Supervisor
4. 按依赖顺序启动服务:
   ┌──────────┐
   │ Priority │  Service
   ├──────────┤
   │    0     │  cached (状态恢复)
   │    1     │  ledger (账本)
   │    2     │  archive (归档)
   │    3     │  md (行情，可多个)
   │    4     │  td (交易，可多个)
   │    5     │  strategy (策略，可多个)
   │    6     │  kf-api (API 网关)
   └──────────┘
5. 进入事件循环 + 监控循环
6. 接收子进程注册、通道请求
7. 异常退出时按逆序优雅停止
```

### 5.2 Ledger 模块

#### 5.2.1 核心职责

| 职责 | 说明 |
|------|------|
| 账户账本管理 | 维护各账户的资产和持仓 |
| 交易数据持久化 | 订单、成交数据写入 SQLite |
| 手续费计算 | 根据配置计算交易手续费 |
| 实时账本更新 | 响应订单和成交事件更新账本 |
| Book 管理 | 支持不同品种的持仓计算（股票/期货/加密货币） |

#### 5.2.2 类结构

```cpp
// ledger/ledger.hpp
namespace kungfu::ledger {

class Ledger : public yijinjing::practice::apprentice {
public:
    Ledger(const io::location_ptr& home, bool low_latency);
    
    void react() override;
    void on_start() override;
    
    // 事件处理
    void on_order(const event_ptr& event);
    void on_trade(const event_ptr& event);
    void on_position(const event_ptr& event);
    void on_asset(const event_ptr& event);
    
private:
    std::unique_ptr<BookKeeper> book_keeper_;
    std::unique_ptr<CommissionManager> commission_mgr_;
    std::unique_ptr<SqliteStore> store_;   // SQLite 持久化
};

// 账本管理器
class BookKeeper {
public:
    void on_trade(const Trade& trade, const location_ptr& source);
    void on_order(const Order& order, const location_ptr& source);
    
    Position get_position(const std::string& account_id, const std::string& instrument_id, Direction dir) const;
    Asset get_asset(const std::string& account_id) const;
    
private:
    // 按品种使用不同的计算器
    std::unique_ptr<PositionCalculator> get_calculator(InstrumentType type);
    
    std::unordered_map<std::string, Asset> assets_;
    std::unordered_map<std::string, std::vector<Position>> positions_;
};

} // namespace kungfu::ledger
```

### 5.3 Cached 模块

#### 5.3.1 核心职责

| 职责 | 说明 |
|------|------|
| 状态恢复 | 启动时从 Journal 重放事件恢复系统状态 |
| 缓存维护 | 维护最新的行情快照、订单状态 |
| 查询服务 | 为其他进程提供历史状态查询 |

#### 5.3.2 类结构

```cpp
// cached/cached.hpp
namespace kungfu::cached {

class Cached : public yijinjing::practice::apprentice {
public:
    Cached(const io::location_ptr& home, bool low_latency);
    
    void react() override;
    void on_start() override;
    
private:
    // 启动时重放 Journal 恢复状态
    void restore_state();
    
    // 缓存最新状态
    std::unordered_map<std::string, Quote> last_quotes_;
    std::unordered_map<uint64_t, Order> active_orders_;
    std::unordered_map<std::string, Position> positions_;
    
    std::unique_ptr<SqliteStore> cache_store_;
};

} // namespace kungfu::cached
```

### 5.4 MD（行情数据）模块

#### 5.4.1 核心职责

| 职责 | 说明 |
|------|------|
| 行情订阅 | 订阅指定合约的实时行情 |
| 行情分发 | 将行情数据写入公共 Journal（广播） |
| 合约管理 | 维护合约信息 |
| 连接管理 | 维护与交易所的连接状态，支持断线重连 |

#### 5.4.2 类结构

```cpp
// md/md.hpp
namespace kungfu::broker {

class MarketDataVendor : public yijinjing::practice::apprentice {
public:
    MarketDataVendor(const io::location_ptr& home, bool low_latency);
    
    void react() override;
    void on_start() override;
    
private:
    std::unique_ptr<MarketData> service_;
};

// 行情适配器接口（扩展模块实现）
class MarketData {
public:
    virtual ~MarketData() = default;
    
    virtual bool connect() = 0;
    virtual bool subscribe(const std::vector<InstrumentKey>& keys) = 0;
    virtual bool unsubscribe(const std::vector<InstrumentKey>& keys) = 0;
    virtual void on_start() = 0;
    virtual void on_custom_event(const event_ptr& event) {}
    
    // 状态
    BrokerState get_state() const { return state_; }
    
protected:
    // 子类调用这些方法将数据写入 Journal
    void on_quote(const Quote& quote);
    void on_bar(const Bar& bar);
    void on_instruments(const std::vector<Instrument>& instruments);
    void update_broker_state(BrokerState state);
    
    BrokerState state_ = BrokerState::Idle;
    std::unordered_map<std::string, Instrument> instruments_;
    std::vector<InstrumentKey> pending_subscribes_;
};

} // namespace kungfu::broker
```

### 5.5 TD（交易）模块

#### 5.5.1 核心职责

| 职责 | 说明 |
|------|------|
| 订单管理 | 订单提交、撤销、状态更新 |
| 账户管理 | 获取账户资产信息 |
| 持仓管理 | 获取持仓信息 |
| 自成交检测 | 防止同一账户内的自成交 |
| 断线恢复 | 重连后恢复订单/持仓状态 |

#### 5.5.2 类结构

```cpp
// td/td.hpp
namespace kungfu::broker {

class TraderVendor : public yijinjing::practice::apprentice {
public:
    TraderVendor(const io::location_ptr& home, bool low_latency);
    
    void react() override;
    void on_start() override;
    
private:
    std::unique_ptr<Trader> service_;
};

// 交易适配器接口（扩展模块实现）
class Trader {
public:
    virtual ~Trader() = default;
    
    virtual bool connect() = 0;
    virtual bool login() = 0;
    virtual bool insert_order(const OrderInput& input) = 0;
    virtual bool cancel_order(uint64_t order_id) = 0;
    virtual bool req_position() = 0;
    virtual bool req_account() = 0;
    virtual void on_start() = 0;
    virtual void on_recover() {}
    
    BrokerState get_state() const { return state_; }
    
protected:
    // 子类调用这些方法将数据写入 Journal
    void on_order(const Order& order);
    void on_trade(const Trade& trade);
    void on_position(const Position& position);
    void on_asset(const Asset& asset);
    void update_broker_state(BrokerState state);
    
    // 自成交检测
    bool has_self_deal_risk(const OrderInput& input) const;
    
    BrokerState state_ = BrokerState::Idle;
    bool self_deal_detect_ = true;
    std::unordered_map<uint64_t, Order> orders_;
};

} // namespace kungfu::broker
```

### 5.6 Strategy（策略引擎）模块

#### 5.6.1 核心职责

| 职责 | 说明 |
|------|------|
| 策略生命周期管理 | 启动前、启动后、停止前、停止后回调 |
| 事件驱动回调 | 行情、订单、成交等事件的处理 |
| 交易操作接口 | 下单、撤单、查询等操作 |
| 定时任务 | 单次定时器和周期定时器 |
| 动态库加载 | 从 .so/.dll 加载用户策略 |

#### 5.6.2 类结构

```cpp
// strategy/strategy.hpp
namespace kungfu::wingchun {

class StrategyRunner : public yijinjing::practice::apprentice {
public:
    StrategyRunner(const io::location_ptr& home, const std::string& strategy_path, bool low_latency);
    
    void react() override;
    void on_start() override;
    
private:
    void load_strategy(const std::string& path);
    
    void* lib_handle_;             // dlopen/LoadLibrary 句柄
    std::unique_ptr<Strategy> strategy_;
    std::unique_ptr<Context> context_;
};

// 策略接口（用户实现）
class Strategy {
public:
    virtual ~Strategy() = default;
    
    // 生命周期回调
    virtual void pre_start(Context* context) {}
    virtual void post_start(Context* context) {}
    virtual void pre_stop(Context* context) {}
    virtual void post_stop(Context* context) {}
    
    // 行情回调
    virtual void on_quote(Context* context, const Quote& quote, const location_ptr& location) {}
    virtual void on_bar(Context* context, const Bar& bar, const location_ptr& location) {}
    
    // 订单回调
    virtual void on_order(Context* context, const Order& order, const location_ptr& location) {}
    virtual void on_trade(Context* context, const Trade& trade, const location_ptr& location) {}
    
    // 定时器回调
    virtual void on_timer(Context* context, int64_t nanotime) {}
};

// 策略上下文（提供交易操作接口）
class Context {
public:
    // 交易操作
    uint64_t insert_order(const std::string& instrument_id, const std::string& exchange_id,
                          double price, int64_t volume, Side side, Offset offset,
                          PriceType price_type = PriceType::Limit);
    uint64_t cancel_order(uint64_t order_id);
    
    // 行情订阅
    void subscribe(const std::string& source, const std::vector<std::string>& instruments);
    
    // 定时任务
    void add_timer(int64_t trigger_nanotime, std::function<void(event_ptr)> callback);
    void add_time_interval(int64_t duration_ns, std::function<void(event_ptr)> callback);
    
    // 数据查询
    int64_t now() const;
    std::string get_trading_day() const;
    Position get_position(const std::string& instrument_id, Direction direction) const;
    Asset get_asset() const;
    
    // 账户绑定
    void set_account(const std::string& source, const std::string& account);
};

} // namespace kungfu::wingchun
```

#### 5.6.3 策略 SDK 头文件

用户开发策略只需 include 一个头文件：

```cpp
// include/kungfu/strategy.h — 策略开发者唯一需要包含的头文件
#pragma once

#include <kungfu/longfist/types.h>
#include <kungfu/longfist/enums.h>
#include <kungfu/wingchun/strategy.hpp>
#include <kungfu/wingchun/context.hpp>

// 策略导出宏
#ifdef _WIN32
#define KF_STRATEGY_EXPORT __declspec(dllexport)
#else
#define KF_STRATEGY_EXPORT __attribute__((visibility("default")))
#endif

#define KUNGFU_STRATEGY(ClassName) \
    extern "C" KF_STRATEGY_EXPORT kungfu::wingchun::Strategy* create_strategy() { \
        return new ClassName(); \
    } \
    extern "C" KF_STRATEGY_EXPORT void destroy_strategy(kungfu::wingchun::Strategy* s) { \
        delete s; \
    }
```

用户策略示例：

```cpp
// my_strategy.cpp
#include <kungfu/strategy.h>

using namespace kungfu::wingchun;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;

class MyStrategy : public Strategy {
public:
    void pre_start(Context* ctx) override {
        ctx->set_account("xtp", "account001");
        ctx->subscribe("xtp", {"600000", "000001"});
    }
    
    void on_quote(Context* ctx, const Quote& quote, const location_ptr& loc) override {
        if (quote.last_price < 10.0) {
            ctx->insert_order(quote.instrument_id, quote.exchange_id,
                            quote.ask_price[0], 100, Side::Buy, Offset::Open);
        }
    }
    
    void on_trade(Context* ctx, const Trade& trade, const location_ptr& loc) override {
        // 成交回调处理
    }
};

KUNGFU_STRATEGY(MyStrategy)
```

### 5.7 API 网关模块（Boost.Beast）

#### 5.7.1 核心职责

| 职责 | 说明 |
|------|------|
| RESTful API | 基于 Boost.Beast 提供 HTTP 接口 |
| WebSocket | 基于 Boost.Beast 提供实时数据推送 |
| 认证鉴权 | JWT Token 认证机制 |
| 日志审计 | 记录 API 调用日志 |

#### 5.7.2 API 接口设计

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

**系统管理**

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/v1/system/status` | GET | 获取系统状态（各进程状态） |
| `/api/v1/system/services/{name}/restart` | POST | 重启指定服务 |
| `/api/v1/system/config` | GET/PUT | 获取/更新系统配置 |

**WebSocket 实时推送**

| 通道 | 说明 |
|------|------|
| `quote.{instrument_id}` | 实时行情推送 |
| `order.{account_id}` | 订单状态推送 |
| `trade.{account_id}` | 成交推送 |
| `position.{account_id}` | 持仓变化推送 |
| `system.status` | 系统状态变化推送 |

#### 5.7.3 类结构

```cpp
// api/api_gateway.hpp
namespace kungfu::api {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class ApiGateway : public yijinjing::practice::apprentice {
public:
    ApiGateway(const io::location_ptr& home, const std::string& host, uint16_t port, bool low_latency);
    
    void react() override;
    void on_start() override;
    void on_exit() override;
    
private:
    // HTTP 路由分发
    void handle_request(http::request<http::string_body>&& req,
                       std::shared_ptr<HttpSession> session);
    
    // REST 处理器
    void handle_accounts(const http::request<http::string_body>& req, HttpResponse& res);
    void handle_orders(const http::request<http::string_body>& req, HttpResponse& res);
    void handle_market(const http::request<http::string_body>& req, HttpResponse& res);
    void handle_strategies(const http::request<http::string_body>& req, HttpResponse& res);
    void handle_system(const http::request<http::string_body>& req, HttpResponse& res);
    
    // WebSocket 管理
    void on_ws_connect(std::shared_ptr<WsSession> session);
    void on_ws_message(std::shared_ptr<WsSession> session, const std::string& message);
    void broadcast_to_subscribers(const std::string& channel, const nlohmann::json& data);
    
    // JWT 认证
    bool authenticate(const http::request<http::string_body>& req);
    
    net::io_context ioc_;
    std::shared_ptr<tcp::acceptor> acceptor_;
    std::unordered_map<std::string, std::vector<std::weak_ptr<WsSession>>> ws_subscriptions_;
};

// HTTP 会话
class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket&& socket, ApiGateway& gateway);
    void run();
private:
    void do_read();
    void do_write(http::response<http::string_body>&& response);
    
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    ApiGateway& gateway_;
};

// WebSocket 会话
class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    WsSession(tcp::socket&& socket, ApiGateway& gateway);
    void run();
    void send(const std::string& message);
private:
    void do_read();
    
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    ApiGateway& gateway_;
    std::vector<std::string> subscribed_channels_;
};

} // namespace kungfu::api
```

---

## 六、扩展机制设计

### 6.1 扩展模块结构

```
src/extensions/
├── xtp/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── trader_xtp.hpp
│   │   └── marketdata_xtp.hpp
│   ├── src/
│   │   ├── trader_xtp.cpp
│   │   └── marketdata_xtp.cpp
│   └── lib/                   # XTP SDK 库文件
│       ├── linux/
│       └── windows/
├── ctp/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── trader_ctp.hpp
│   │   └── marketdata_ctp.hpp
│   ├── src/
│   │   ├── trader_ctp.cpp
│   │   └── marketdata_ctp.cpp
│   └── lib/                   # CTP SDK 库文件
│       ├── linux/
│       └── windows/
└── sim/
    ├── CMakeLists.txt
    ├── include/
    │   └── trader_sim.hpp
    └── src/
        ├── trader_sim.cpp     # 模拟交易（多种撮合模式）
        └── marketdata_sim.cpp # 模拟行情
```

### 6.2 扩展加载机制

```cpp
// extension/extension_manager.hpp
namespace kungfu::extension {

class ExtensionManager {
public:
    static std::unique_ptr<broker::MarketData> create_md(const std::string& name,
                                                         broker::MarketDataVendor& vendor);
    static std::unique_ptr<broker::Trader> create_td(const std::string& name,
                                                      broker::TraderVendor& vendor);
    
private:
    static void* load_library(const std::string& name);
    
    // 平台相关的库文件路径
    static std::string get_library_path(const std::string& name) {
#ifdef _WIN32
        return "extensions/" + name + ".dll";
#elif __APPLE__
        return "extensions/lib" + name + ".dylib";
#else
        return "extensions/lib" + name + ".so";
#endif
    }
};

} // namespace kungfu::extension
```

### 6.3 扩展接口规范

每个扩展模块需要导出以下 C 接口：

```cpp
// 扩展模块导出宏
#ifdef _WIN32
#define KF_EXTENSION_EXPORT __declspec(dllexport)
#else
#define KF_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
    KF_EXTENSION_EXPORT broker::MarketData* create_market_data(broker::MarketDataVendor* vendor);
    KF_EXTENSION_EXPORT void destroy_market_data(broker::MarketData* instance);
    
    KF_EXTENSION_EXPORT broker::Trader* create_trader(broker::TraderVendor* vendor);
    KF_EXTENSION_EXPORT void destroy_trader(broker::Trader* instance);
}
```

---

## 七、数据持久化设计

### 7.1 存储架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        存储架构                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  热数据 (交易日内)                                              │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Journal Files (mmap 共享内存)                           │  │
│  │  - 高速追加写入                                          │  │
│  │  - 零拷贝读取                                            │  │
│  │  - 按交易日/进程/通道组织                                │  │
│  └──────────────────────────────────────────────────────────┘  │
│                            │                                    │
│                            ▼ (Cached 重放)                      │
│  温数据 (查询/恢复)                                             │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  SQLite Database                                         │  │
│  │  - 订单/成交/持仓/资产 表                                │  │
│  │  - Ledger 写入, API/Cached 读取                          │  │
│  │  - 自动基于 Boost.Hana 生成表结构                        │  │
│  └──────────────────────────────────────────────────────────┘  │
│                            │                                    │
│                            ▼ (Archive 清理)                     │
│  冷数据 (归档)                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  归档 Journal 文件 (压缩存储)                            │  │
│  │  - 按交易日归档                                          │  │
│  │  - 超过保留期限自动清理                                  │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 7.2 SQLite 表结构

基于 Boost.Hana 自动生成，核心表包括：

| 表名 | 说明 | 数据来源 |
|------|------|----------|
| `orders` | 订单记录 | TD → Journal → Ledger |
| `trades` | 成交记录 | TD → Journal → Ledger |
| `positions` | 持仓快照 | Ledger 计算 |
| `assets` | 资产快照 | Ledger 计算 |
| `instruments` | 合约信息 | MD |
| `trading_days` | 交易日历 | Master |
| `commissions` | 手续费配置 | 配置文件导入 |

### 7.3 数据生命周期

| 阶段 | 存储位置 | 保留时间 | 管理者 |
|------|----------|----------|--------|
| 实时 | Journal (mmap) | 当前交易日 | 各进程 Writer |
| 近期 | Journal 文件 (磁盘) | 7 天 | Archive |
| 持久 | SQLite | 永久（可配置） | Ledger |
| 归档 | 压缩 Journal | 可配置 | Archive |

---

## 八、配置文件设计

### 8.1 统一配置文件 (kungfu.toml)

```toml
# kungfu.toml - 系统统一配置文件

[system]
name = "kungfu-trader"
home = "/var/kungfu"                    # 数据根目录
log_level = "info"                      # trace/debug/info/warn/error
low_latency = false                     # 低延迟模式（CPU 亲和性、忙等待）
trading_day = ""                        # 留空则自动判断

[system.journal]
page_size = 134217728                   # Journal Page 大小 (128MB)
archive_days = 7                        # Journal 保留天数
archive_compress = true                 # 归档是否压缩

[master]
log_level = "info"

[master.supervisor]
monitor_interval_ms = 1000              # 监控检查间隔
default_restart_policy = "on_failure"   # always / on_failure / never
default_max_restart_count = 3           # 窗口内最大重启次数
default_restart_window_seconds = 60     # 重启计数窗口
default_restart_delay_ms = 2000         # 重启延迟

[api]
host = "127.0.0.1"
port = 8080
ws_port = 8081
jwt_secret = "your-secret-key"
jwt_expire_hours = 24
log_level = "info"

[ledger]
log_level = "info"
db_path = ""                            # 留空则使用 {home}/db/ledger.db

[cached]
log_level = "info"

[archive]
log_level = "info"
schedule = "02:00"                      # 每日归档时间

# 行情源配置（可多个）
[[md]]
source = "xtp"                          # 扩展名称
name = "xtp"                            # 实例名称
extension = "xtp"                       # 动态库名称
restart_policy = "always"

[md.config]                             # 扩展自定义配置
server_ip = "120.27.164.138"
server_port = 6002
user_id = "your_user_id"
password = "your_password"
protocol = 1                            # 1=TCP, 2=UDP

# 交易账户配置（可多个）
[[td]]
source = "xtp"
name = "account001"
extension = "xtp"
restart_policy = "always"
self_deal_detect = true                 # 自成交检测

[td.config]
server_ip = "120.27.164.69"
server_port = 6001
user_id = "your_user_id"
password = "your_password"
key = "your_key"

# 模拟交易配置
[[td]]
source = "sim"
name = "sim001"
extension = "sim"
restart_policy = "on_failure"
self_deal_detect = false

[td.config]
match_mode = "fill"                     # fill / reject / cancel / partial
latency_ms = 10                         # 模拟延迟

# 策略配置（可多个）
[[strategy]]
name = "my_strategy"
path = "/path/to/my_strategy.so"        # 策略动态库路径
account = "xtp.account001"              # 绑定的交易账户
restart_policy = "on_failure"

# 手续费配置
[commission]
default_rate = 0.0003                   # 默认费率

[[commission.rules]]
exchange_id = "SSE"
instrument_type = "stock"
open_rate = 0.0003
close_rate = 0.0003
min_commission = 5.0

[[commission.rules]]
exchange_id = "SHFE"
instrument_type = "future"
open_rate_by_money = 0.000023
close_rate_by_money = 0.000023
close_today_rate_by_money = 0.0
```

---

## 九、错误处理与监控

### 9.1 断线重连机制

```cpp
// common/reconnect.hpp
namespace kungfu::common {

struct ReconnectConfig {
    int initial_delay_ms = 1000;        // 首次重连延迟
    int max_delay_ms = 30000;           // 最大重连延迟
    double backoff_multiplier = 2.0;    // 退避倍数
    int max_attempts = 0;              // 最大尝试次数 (0=无限)
};

class ReconnectManager {
public:
    ReconnectManager(ReconnectConfig config, std::function<bool()> connect_fn);
    
    void start();
    void stop();
    void on_connected();
    void on_disconnected();
    
private:
    void schedule_reconnect();
    
    ReconnectConfig config_;
    std::function<bool()> connect_fn_;
    int current_delay_ms_;
    int attempt_count_;
    bool running_;
};

} // namespace kungfu::common
```

### 9.2 健康检查

Master 通过以下方式监控子进程健康状态：

| 检查方式 | 说明 |
|----------|------|
| 进程存活 | 操作系统级进程状态检查 (waitpid / WaitForSingleObject) |
| 心跳检测 | 子进程定期向 Master Journal 写入心跳 Frame |
| 超时检测 | 超过配置时间未收到心跳则判定为异常 |

### 9.3 系统指标

```cpp
// common/metrics.hpp
namespace kungfu::common {

struct SystemMetrics {
    // 延迟统计
    int64_t quote_latency_ns;           // 行情延迟
    int64_t order_latency_ns;           // 下单延迟
    int64_t trade_latency_ns;           // 成交回报延迟
    
    // 吞吐统计
    int64_t quotes_per_second;          // 行情吞吐
    int64_t orders_per_second;          // 下单吞吐
    
    // Journal 统计
    int64_t journal_write_count;
    int64_t journal_read_count;
    int64_t journal_total_bytes;
    
    // 进程统计
    int64_t memory_usage_bytes;
    double cpu_usage_percent;
};

} // namespace kungfu::common
```

---

## 十、编译与部署

### 10.1 依赖管理 (git submodule)

所有第三方依赖通过 git submodule 统一管理在 `3rdparty/` 目录下，通过 CMake 的 `add_subdirectory` 直接集成编译。

```bash
# .gitmodules
[submodule "3rdparty/boost"]
    path = 3rdparty/boost
    url = https://github.com/boostorg/boost.git
    branch = boost-1.84.0

[submodule "3rdparty/nng"]
    path = 3rdparty/nng
    url = https://github.com/nanomsg/nng.git
    branch = v1.7.3

[submodule "3rdparty/nlohmann"]
    path = 3rdparty/nlohmann
    url = https://github.com/nlohmann/json.git
    branch = v3.11.3

[submodule "3rdparty/spdlog"]
    path = 3rdparty/spdlog
    url = https://github.com/gabime/spdlog.git
    branch = v1.12.0

[submodule "3rdparty/sqlite3"]
    path = 3rdparty/sqlite3
    url = https://github.com/aspect-build/sqlite.git
    branch = version-3.43.2

[submodule "3rdparty/sqlite_orm"]
    path = 3rdparty/sqlite_orm
    url = https://github.com/fnc12/sqlite_orm.git
    branch = v1.8.2

[submodule "3rdparty/tomlplusplus"]
    path = 3rdparty/tomlplusplus
    url = https://github.com/marzer/tomlplusplus.git
    branch = v3.4.0

[submodule "3rdparty/rxcpp"]
    path = 3rdparty/rxcpp
    url = https://github.com/ReactiveX/RxCpp.git
    branch = v4.1.1

[submodule "3rdparty/googletest"]
    path = 3rdparty/googletest
    url = https://github.com/google/googletest.git
    branch = v1.14.0
```

> **关于 Boost 子模块体积**：Boost 完整仓库较大（~200MB），如果只需要 Beast/Hana/Asio 三个库，可以考虑只引入对应的子仓库（boostorg/beast、boostorg/hana、boostorg/asio）并手动管理其依赖，或者使用 Boost 官方的 `boostdep` 工具裁剪。

### 10.2 编译流程

```bash
# 1. 克隆项目（含所有 submodule）
git clone --recursive https://github.com/user/kungfu-cpp.git
cd kungfu-cpp

# 如果已经克隆但未拉取 submodule：
# git submodule update --init --recursive

# 2. 创建构建目录
mkdir build && cd build

# 3. 配置 CMake
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EXTENSIONS=ON \
      -DBUILD_TESTS=ON \
      ..

# 4. 编译
cmake --build . --config Release -j$(nproc)

# 5. 运行测试
ctest --output-on-failure

# 6. 安装
cmake --install . --config Release --prefix /opt/kungfu
```

### 10.3 CMake 结构

```cmake
# CMakeLists.txt (顶层)
cmake_minimum_required(VERSION 3.20)
project(kungfu-cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_EXTENSIONS "Build broker extensions" ON)
option(BUILD_TESTS "Build test suite" ON)
option(LOW_LATENCY "Enable low-latency optimizations" OFF)

# ============================================================
# 第三方依赖 (git submodule, 通过 add_subdirectory 集成)
# ============================================================

# Boost (仅构建需要的库: beast, hana, asio, system)
set(BOOST_INCLUDE_LIBRARIES beast hana asio system)
add_subdirectory(3rdparty/boost EXCLUDE_FROM_ALL)

# nng
set(NNG_TESTS OFF CACHE BOOL "" FORCE)
set(NNG_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/nng EXCLUDE_FROM_ALL)

# nlohmann_json (header-only)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/nlohmann EXCLUDE_FROM_ALL)

# spdlog
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/spdlog EXCLUDE_FROM_ALL)

# SQLite3 (amalgamation 编译)
add_subdirectory(3rdparty/sqlite3 EXCLUDE_FROM_ALL)

# sqlite_orm (header-only)
set(SQLITE_ORM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/sqlite_orm EXCLUDE_FROM_ALL)

# tomlplusplus (header-only)
add_subdirectory(3rdparty/tomlplusplus EXCLUDE_FROM_ALL)

# rxcpp (header-only)
add_subdirectory(3rdparty/rxcpp EXCLUDE_FROM_ALL)

# ============================================================
# 项目模块
# ============================================================

# 核心库
add_subdirectory(src/core/longfist)
add_subdirectory(src/core/yijinjing)
add_subdirectory(src/core/wingchun)
add_subdirectory(src/common)

# 服务
add_subdirectory(src/services/master)
add_subdirectory(src/services/ledger)
add_subdirectory(src/services/cached)
add_subdirectory(src/services/archive)
add_subdirectory(src/services/md)
add_subdirectory(src/services/td)
add_subdirectory(src/services/strategy)
add_subdirectory(src/services/api)

# 工具
add_subdirectory(src/tools)

# 扩展
if(BUILD_EXTENSIONS)
    add_subdirectory(src/extensions/sim)
    add_subdirectory(src/extensions/xtp)
    add_subdirectory(src/extensions/ctp)
endif()

# 测试
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(3rdparty/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
```

### 10.4 编译产物

```
install/
├── bin/
│   ├── master                  # Master 进程 (含 Supervisor)
│   ├── ledger                  # Ledger 进程
│   ├── cached                  # Cached 进程
│   ├── archive                 # Archive 进程
│   ├── md                      # MD 进程
│   ├── td                      # TD 进程
│   ├── strategy                # Strategy 进程
│   ├── kf-api                  # API 网关
│   ├── kf-status               # 服务状态查看工具
│   ├── kf-log                  # 日志查看工具
│   └── kf-config               # 配置管理工具
├── lib/
│   ├── libkf-core.so           # 核心框架库
│   └── extensions/
│       ├── libxtp.so           # XTP 扩展 (.dll on Windows)
│       ├── libctp.so           # CTP 扩展
│       └── libsim.so           # 模拟交易扩展
├── include/
│   └── kungfu/
│       └── strategy.h          # 策略开发 SDK 头文件
└── config/
    └── kungfu.toml             # 默认配置模板
```

### 10.5 启动流程

```bash
# 方式一：Master 统一管理（推荐）
# Master 会根据配置文件自动启动所有子服务
./bin/master -c config/kungfu.toml

# 方式二：手动启动各服务（调试用）
./bin/master -c config/kungfu.toml --no-supervisor  # 仅协调，不启动子进程
./bin/ledger -c config/kungfu.toml
./bin/cached -c config/kungfu.toml
./bin/md -c config/kungfu.toml -g xtp -n xtp
./bin/td -c config/kungfu.toml -g xtp -n account001
./bin/strategy -c config/kungfu.toml -n my_strategy /path/to/strategy.so
./bin/kf-api -c config/kungfu.toml
```

---

## 十一、Qt UI 子项目设计

### 11.1 项目结构

```
kungfu-qt/
├── src/
│   ├── main.cpp
│   ├── mainwindow.cpp/.h/.ui
│   ├── widgets/
│   │   ├── order_widget.cpp/.h
│   │   ├── position_widget.cpp/.h
│   │   ├── strategy_widget.cpp/.h
│   │   └── market_widget.cpp/.h
│   ├── api/
│   │   ├── rest_client.cpp/.h    # REST API 客户端
│   │   ├── ws_client.cpp/.h      # WebSocket 客户端
│   │   └── models/               # 数据模型
│   └── resources/
├── CMakeLists.txt
└── README.md
```

### 11.2 UI 模块划分

| 模块 | 功能 |
|------|------|
| **策略管理** | 策略列表、启动/停止、参数配置 |
| **订单监控** | 实时订单状态、成交记录 |
| **账户管理** | 账户列表、资产查询、持仓查询 |
| **系统配置** | 服务配置、扩展管理 |
| **行情展示** | 实时行情、K线图表 |
| **系统监控** | 进程状态、延迟统计、日志查看 |

### 11.3 通信协议

- **REST API**：用于同步数据查询（账户、订单列表等）
- **WebSocket**：用于实时数据推送（行情、订单状态变化、系统状态等）

---

## 十二、第三方依赖清单

### 核心依赖（git submodule 管理）

| 依赖 | 版本 | 用途 | License | 备注 |
|------|------|------|---------|------|
| **Boost** | 1.84+ | Beast (HTTP/WS)、Hana (编译期反射)、Asio (异步IO) | BSL-1.0 | 可裁剪只保留需要的子库 |
| **nng** | 1.7+ | 进程间控制消息 (nanomsg-next-gen) | MIT | 需编译 |
| **nlohmann_json** | 3.11+ | JSON 解析 | MIT | header-only |
| **spdlog** | 1.12+ | 日志库 | MIT | 需编译 |
| **SQLite3** | 3.43+ | 嵌入式数据库 | Public Domain | amalgamation 单文件编译 |
| **sqlite_orm** | 1.8+ | SQLite ORM | BSD-3 | header-only |
| **tomlplusplus** | 3.4+ | TOML 配置解析 | MIT | header-only |
| **rxcpp** | 4.1+ | 响应式编程（事件循环） | Apache 2.0 | header-only |
| **Google Test** | 1.14+ | 测试框架 | BSD-3 | 仅测试时编译 |

### 可选依赖

| 依赖 | 用途 | License |
|------|------|---------|
| **Qt 6** | UI 框架（独立子项目） | LGPL 3.0 |
| **XTP API** | XTP 交易所接口（扩展模块） | 商业授权 |
| **CTP API** | CTP 交易所接口（扩展模块） | 商业授权 |

### 内部核心框架

| 模块 | 用途 |
|------|------|
| **yijinjing** | 事件总线、Journal 共享内存通信、进程协作 |
| **longfist** | 数据类型定义、Boost.Hana 编译期反射、自动序列化 |
| **wingchun** | 策略引擎核心、Broker 抽象、Book 管理 |

---

## 十三、安全与性能

### 13.1 安全考虑

| 安全项 | 措施 |
|--------|------|
| **API 认证** | JWT Token 认证机制 |
| **权限控制** | 基于角色的访问控制（RBAC） |
| **数据加密** | HTTPS/WSS 传输加密（Boost.Beast + OpenSSL） |
| **输入验证** | 严格的参数校验（订单参数范围检查） |
| **日志审计** | 完整的操作日志记录 |
| **进程隔离** | 各进程独立运行，崩溃不影响其他服务 |

### 13.2 性能优化

| 优化项 | 措施 |
|--------|------|
| **共享内存** | 使用 mmap 实现零拷贝 IPC |
| **无锁设计** | Journal 单写者模型避免锁竞争 |
| **内存映射** | Page 预分配避免运行时分配 |
| **批量处理** | 支持批量订单处理 |
| **CPU 亲和性** | low_latency 模式下绑定 CPU 核心 |
| **忙等待** | low_latency 模式下 Reader 使用 spin-wait |
| **编译期序列化** | Boost.Hana 零开销类型反射 |
| **异步 IO** | Boost.Asio 事件驱动（API 层） |

### 13.3 性能指标目标

| 指标 | 目标值 |
|------|--------|
| Journal 写入延迟 | < 1 μs |
| 行情端到端延迟 | < 10 μs (low_latency 模式) |
| 下单端到端延迟 | < 20 μs (low_latency 模式) |
| 行情吞吐量 | > 100,000 quotes/s |
| 订单吞吐量 | > 10,000 orders/s |

---

## 十四、开发路线图

### Phase 1: 基础框架（4-6 周）
- [ ] Yijinjing Journal 系统（Page/Frame/Reader/Writer）
- [ ] IO Locator
- [ ] Longfist 数据类型 + Boost.Hana 序列化
- [ ] Practice 层（hero/apprentice）
- [ ] 基础事件循环（rxcpp）
- [ ] 单元测试

### Phase 2: 核心服务（4-6 周）
- [ ] Master + Supervisor
- [ ] Ledger（含 BookKeeper）
- [ ] Cached（状态恢复）
- [ ] Archive（Journal 归档）
- [ ] 集成测试

### Phase 3: 交易引擎（4-6 周）
- [ ] MD/TD Vendor 框架
- [ ] Strategy Runner + Context
- [ ] 策略 SDK
- [ ] SIM 模拟交易扩展
- [ ] 端到端测试

### Phase 4: API + 扩展（3-4 周）
- [ ] Boost.Beast API 网关
- [ ] WebSocket 实时推送
- [ ] XTP 扩展
- [ ] CTP 扩展
- [ ] 性能基准测试

### Phase 5: UI + 优化（4-6 周）
- [ ] Qt UI 子项目
- [ ] 低延迟优化
- [ ] 文档完善
- [ ] 部署打包
