# Kungfu-cpp 服务启动时序图

## 概述

本文档详细描述 Kungfu-cpp 系统中所有服务的启动流程和消息交互时序。系统采用分布式架构，各服务通过 Master 协调启动。

## 服务启动顺序

```
Master → Cached → Ledger → MD → TD → Strategy
```

---

## 一、Master 服务启动

### 时序图

```
Master
  │
  ├─[1] 创建 io_device_master
  │      └─ location: system/master/master/live
  │
  ├─[2] profile_.setup()
  │      └─ 加载所有 Location 和 Config 配置
  │
  ├─[3] session_builder_.open_session(master_home_location_, start_time_)
  │
  ├─[4] get_writer(location::PUBLIC)->mark(SessionStart::tag)
  │      └─ 向 PUBLIC 广播会话开始
  │
  └─[5] run()
         └─ 进入事件循环，等待其他服务注册
```

### 关键消息

| 步骤 | 消息类型 | 源 | 目标 | 说明 |
|------|----------|-----|------|------|
| 4 | SessionStart | Master | PUBLIC | 标记会话开始 |

---

## 二、Cached 服务启动

### 时序图

```
Cached                              Master
  │                                    │
  ├─[1] 创建 io_device_client        │
  │      └─ location: system/service/cached/live
  │                                    │
  ├─[2] checkin() ──────────────────>│
  │      └─ 发送 Register JSON 请求   │
  │                                    │
  │                                    ├─[3] register_app()
  │                                    │      ├─ 创建 app_location
  │                                    │      ├─ 创建 master_cmd_location
  │                                    │      ├─ open_session(app_location)
  │                                    │      ├─ app_cmd_writer->mark(SessionStart)
  │                                    │      ├─ public_writer->write(Location)
  │                                    │      ├─ public_writer->write(Register)
  │                                    │      ├─ require_write_to(PUBLIC)
  │                                    │      ├─ require_write_to(SYNC)
  │                                    │      ├─ write_time_reset()
  │                                    │      ├─ write_trading_day()
  │                                    │      ├─ write_locations()
  │                                    │      └─ write_registries()
  │                                    │
  ├─[4] 收到 Register (self) <─────────│
  │      ├─ last_active_time 更新     │
  │      └─ reader_->join(master_cmd) │
  │                                    │
  ├─[5] 收到 TimeReset <──────────────│
  │      └─ time::reset()             │
  │                                    │
  ├─[6] 收到 TradingDay <─────────────│
  │                                    │
  ├─[7] 收到 Location/Registry <──────│
  │                                    │
  ├─[8] expect_start()               │
  │      └─ 等待 RequestStart         │
  │                                    │
  ├─[9] get_live_home_uid() ==       │
  │      cached_home_location_->uid   │
  │      └─ 发送 RequestCachedDone ──>│
  │                                    │
  │                                    ├─[10] on_request_cached_done()
  │                                    │      ├─ app_cmd_writer->mark(RequestStart)
  │                                    │      ├─ write_locations()
  │                                    │      ├─ write_registries()
  │                                    │      ├─ write_channels()
  │                                    │      └─ write_bands()
  │                                    │
  ├─[11] 收到 RequestStart <──────────│
  │      └─ started_ = true           │
  │      └─ on_start()                │
  │                                    │
  └─[12] 进入业务事件循环              │
```

### 关键消息

| 步骤 | 消息类型 | 源 | 目标 | 说明 |
|------|----------|-----|------|------|
| 2 | Register(JSON) | Cached | Master | 注册请求 |
| 4 | Register | Master | Cached | 注册确认 |
| 5 | TimeReset | Master | Cached | 时钟同步 |
| 6 | TradingDay | Master | Cached | 交易日 |
| 7 | Location/Registry | Master | Cached | 位置信息 |
| 9 | RequestCachedDone | Cached | Master | 缓存准备完成 |
| 11 | RequestStart | Master | Cached | 启动信号 |

---

## 三、Ledger 服务启动

### 时序图

```
Ledger                              Master                          Cached
  │                                    │                                │
  ├─[1] 创建 io_device_client        │                                │
  │      └─ location: system/service/ledger/live
  │                                    │                                │
  ├─[2] checkin() ──────────────────>│                                │
  │                                    │                                │
  │                                    ├─[3] register_app()            │
  │                                    │      └─ 同 Cached 步骤 3      │
  │                                    │                                │
  ├─[4] 收到 Register (self) <─────────│                                │
  │                                    │                                │
  ├─[5] 收到 TimeReset/TradingDay <────│                                │
  │                                    │                                │
  ├─[6] 收到 Cached 的 Register <──────│                                │
  │                                    │                                │
  ├─[7] request_cached_reader_writer()│                                │
  │      ├─ request_write_to(Cached) ─>│                                │
  │      └─ request_read_from(Cached) ─>│                                │
  │                                    │                                │
  │                                    ├─[8] on_request_write_to()     │
  │                                    │      └─ reader_->join()        │
  │                                    │                                │
  │                                    ├─[9] on_request_read_from()    │
  │                                    │      └─ check_cached_ready_to_read()
  │                                    │            └─ CachedReadyToRead ──>│
  │                                    │                                │
  ├─[10] 收到 CachedReadyToRead <─────│                                │
  │      └─ request_cached(Cached) ───────────────────────────────────>│
  │                                    │                                │
  │                                    │                                ├─[11] 处理缓存恢复
  │                                    │                                │      └─ 发送缓存数据
  │                                    │                                │
  │                                    │                                ├─[12] RequestCachedDone ──>│
  │                                    │                                │
  │                                    ├─[13] on_request_cached_done() │
  │                                    │      └─ RequestStart ─────────>│
  │                                    │                                │
  ├─[14] 收到 RequestStart <──────────│                                │
  │      └─ on_start()                │                                │
  │                                    │                                │
  └─[15] 进入业务事件循环              │                                │
```

### 关键消息

| 步骤 | 消息类型 | 源 | 目标 | 说明 |
|------|----------|-----|------|------|
| 2 | Register(JSON) | Ledger | Master | 注册请求 |
| 7 | RequestWriteTo | Ledger | Master | 请求写入 Cached |
| 7 | RequestReadFrom | Ledger | Master | 请求读取 Cached |
| 9 | CachedReadyToRead | Master | Ledger | Cached 就绪 |
| 10 | RequestCached | Ledger | Cached | 请求缓存数据 |
| 12 | RequestCachedDone | Cached | Master | 缓存完成 |
| 14 | RequestStart | Master | Ledger | 启动信号 |

---

## 四、MD（Market Data）服务启动

### 时序图

```
MD                                  Master                          Cached
  │                                    │                                │
  ├─[1] 创建 io_device_client        │                                │
  │      └─ location: md/sim/sim/live
  │                                    │                                │
  ├─[2] checkin() ──────────────────>│                                │
  │                                    │                                │
  │                                    ├─[3] register_app()            │
  │                                    │                                │
  ├─[4] 收到 Register (self) <─────────│                                │
  │                                    │                                │
  ├─[5] 收到 Cached Register <────────│                                │
  │                                    │                                │
  ├─[6] request_cached_reader_writer()│                                │
  │      └─ 请求读写 Cached ──────────>│                                │
  │                                    │                                │
  ├─[7] 收到 CachedReadyToRead <─────│                                │
  │      └─ request_cached(Cached) ───────────────────────────────────>│
  │                                    │                                │
  │                                    │                                ├─[8] 处理缓存恢复
  │                                    │                                │
  │                                    │                                ├─[9] RequestCachedDone ──>│
  │                                    │                                │
  │                                    ├─[10] RequestStart ────────────>│
  │                                    │                                │
  ├─[11] 收到 RequestStart <──────────│                                │
  │      └─ on_start()                │                                │
  │      └─ update_broker_state(Ready)│                                │
  │                                    │                                │
  └─[12] 等待订阅请求，开始推送行情     │                                │
```

### 关键消息

| 步骤 | 消息类型 | 源 | 目标 | 说明 |
|------|----------|-----|------|------|
| 2 | Register(JSON) | MD | Master | 注册请求 |
| 6 | RequestWriteTo/RequestReadFrom | MD | Master | 请求读写 Cached |
| 7 | CachedReadyToRead | Master | MD | Cached 就绪 |
| 10 | RequestCachedDone | Cached | Master | 缓存完成 |
| 11 | RequestStart | Master | MD | 启动信号 |
| 11 | BrokerStateUpdate | MD | PUBLIC | 状态更新为 Ready |

---

## 五、TD（Trading）服务启动

### 时序图

```
TD                                  Master                          Cached
  │                                    │                                │
  ├─[1] 创建 io_device_client        │                                │
  │      └─ location: td/sim/sim/live
  │                                    │                                │
  ├─[2] checkin() ──────────────────>│                                │
  │                                    │                                │
  │                                    ├─[3] register_app()            │
  │                                    │                                │
  ├─[4] 收到 Register (self) <─────────│                                │
  │                                    │                                │
  ├─[5] 收到 Cached Register <────────│                                │
  │                                    │                                │
  ├─[6] request_cached_reader_writer()│                                │
  │      └─ 请求读写 Cached ──────────>│                                │
  │                                    │                                │
  ├─[7] 收到 CachedReadyToRead <─────│                                │
  │      └─ request_cached(Cached) ───────────────────────────────────>│
  │                                    │                                │
  │                                    │                                ├─[8] 处理缓存恢复
  │                                    │                                │
  │                                    │                                ├─[9] RequestCachedDone ──>│
  │                                    │                                │
  │                                    ├─[10] RequestStart ────────────>│
  │                                    │                                │
  ├─[11] 收到 RequestStart <──────────│                                │
  │      └─ on_start()                │                                │
  │      └─ update_broker_state(Ready)│                                │
  │                                    │                                │
  └─[12] 等待订单请求                 │                                │
```

### 关键消息

| 步骤 | 消息类型 | 源 | 目标 | 说明 |
|------|----------|-----|------|------|
| 2 | Register(JSON) | TD | Master | 注册请求 |
| 6 | RequestWriteTo/RequestReadFrom | TD | Master | 请求读写 Cached |
| 7 | CachedReadyToRead | Master | TD | Cached 就绪 |
| 10 | RequestCachedDone | Cached | Master | 缓存完成 |
| 11 | RequestStart | Master | TD | 启动信号 |
| 11 | BrokerStateUpdate | TD | PUBLIC | 状态更新为 Ready |

---

## 六、Strategy 服务启动（完整流程）

### 时序图

```
Strategy                            Master                          Cached                          Ledger                          MD/TD
  │                                    │                                │                                │                                │
  ├─[1] 创建 io_device_client        │                                │                                │                                │
  │      └─ location: strategy/CppStrategy/demo01exe/live
  │                                    │                                │                                │                                │
  ├─[2] checkin() ──────────────────>│                                │                                │                                │
  │                                    │                                │                                │                                │
  │                                    ├─[3] register_app()            │                                │                                │
  │                                    │                                │                                │                                │
  ├─[4] 收到 Register (self) <─────────│                                │                                │                                │
  │                                    │                                │                                │                                │
  ├─[5] 检查 Cached 是否已注册        │                                │                                │                                │
  │      └─ is_location_live(Cached)  │                                │                                │                                │
  │      └─ 已注册 → 请求缓存读写     │                                │                                │                                │
  │                                    │                                │                                │                                │
  ├─[6] request_cached_reader_writer()│                                │                                │                                │
  │      ├─ request_write_to(Cached) ─>│                                │                                │                                │
  │      └─ request_read_from(Cached) ─>│                                │                                │                                │
  │                                    │                                │                                │                                │
  │                                    ├─[7] on_request_write_to()     │                                │                                │
  │                                    │      └─ require_write_to(Cached)
  │                                    │                                │                                │                                │
  │                                    ├─[8] on_request_read_from()    │                                │                                │
  │                                    │      └─ check_cached_ready_to_read()
  │                                    │            └─ CachedReadyToRead ──>│
  │                                    │                                │                                │                                │
  ├─[9] 收到 CachedReadyToRead <─────│                                │                                │                                │
  │      └─ request_cached(Cached) ─────────────────────────────────────────>│                                │                                │
  │                                    │                                │                                │                                │
  │                                    │                                ├─[10] 处理缓存恢复               │                                │
  │                                    │                                │                                │                                │
  │                                    │                                ├─[11] RequestCachedDone ───────>│                                │
  │                                    │                                │                                │                                │
  │                                    ├─[12] on_request_cached_done() │                                │                                │
  │                                    │      ├─ RequestStart ─────────>│                                │                                │
  │                                    │      ├─ write_locations()      │                                │                                │
  │                                    │      ├─ write_registries()     │                                │                                │
  │                                    │      ├─ write_channels()       │                                │                                │
  │                                    │      └─ write_bands()          │                                │                                │
  │                                    │                                │                                │                                │
  ├─[13] 收到 RequestStart <──────────│                                │                                │                                │
  │      └─ on_start()                │                                │                                │                                │
  │            ├─ pre_start()         │                                │                                │                                │
  │            │      ├─ add_account("sim", "sim")
  │            │      │      └─ 检查 td/sim/sim/live 是否存在
  │            │      │      └─ td_locations_ 记录位置
  │            │      └─ subscribe("sim", {"600000"}, {"SSE"})
  │            │             └─ 建立与 MD 的 Channel
  │            │
  │            ├─ enable(*context_)
  │            └─ events_ | $$(prepare(event))
  │                                    │                                │                                │                                │
  │  ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────│
  │  prepare() 阶段开始，严格顺序执行，每步失败则返回等待下一次事件
  │  ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────│
  │                                    │                                │                                │                                │
  ├─[14] 检查 has_writer(ledger_uid)  │                                │                                │                                │
  │      └─ 若无 writer，等待 Channel 建立
  │                                    │                                │                                │                                │
  │                                    ├─[15] 收到 Channel 事件        │                                │                                │
  │                                    │      ├─ register_channel()     │                                │                                │
  │                                    │      └─ on_write_to() 建立 writer
  │                                    │                                │                                │                                │
  ├─[16] 等待所有账户连接             │                                │                                │                                │
  │      └─ broker_client.is_connected()
  │      └─ 未全部连接 → 返回等待
  │                                    │                                │                                │                                │
  │                                    │                                │                                │                                │
  │                                    │                                │                                │                                ├─[17] MD/TD 连接就绪           │
  │                                    │                                │                                │                                │
  ├─[18] 发送 BrokerStateRequest ──────────────────────────────────────────────────────────────────>│                                │
  │      └─ broker_states_requested_ = true
  │                                    │                                │                                │                                │
  │                                    │                                │                                ├─[19] 处理 BrokerStateRequest  │                                │
  │                                    │                                │                                │      └─ 向 TD 请求状态        │                                │
  │                                    │                                │                                │                                │
  │                                    │                                │                                │                                ├─[20] TD 返回 BrokerStateUpdate│
  │                                    │                                │                                │                                │
  ├─[21] 等待所有服务 Ready           │                                │                                │                                │
  │      └─ broker_client.is_ready()
  │      └─ 未全部 Ready → 返回等待
  │                                    │                                │                                │                                │
  │                                    │                                │                                │                                ├─[22] MD/TD 更新状态为 Ready   │                                │
  │                                    │                                │                                │                                │
  ├─[23] 等待 TD Channel 双向就绪     │                                │                                │                                │
  │      └─ has_channel(get_home_uid(), td_uid)
  │      └─ has_channel(td_uid, get_home_uid())
  │      └─ Channel 未就绪 → 返回等待
  │                                    │                                │                                │                                │
  │                                    │                                │                                │                                ├─[24] TD Channel 建立完成       │                                │
  │                                    │                                │                                │                                │
  ├─[25] 发送 KeepPositionsRequest ─────────────────────────────────────────────────────────────────>│                                │
  ├─[26] 发送 ResetBookRequest ────────────────────────────────────────────────────────────────────>│                                │
  ├─[27] 写入 InstrumentKey ───────────────────────────────────────────────────────────────────────>│                                │
  ├─[28] 发送 RebuildPositionsRequest ─────────────────────────────────────────────────────────────>│                                │
  ├─[29] 发送 AssetRequest ────────────────────────────────────────────────────────────────────────>│                                │
  ├─[30] 发送 PositionRequest ────────────────────────────────────────────────────────────────────>│                                │
  │      └─ positions_requested_ = true
  │                                    │                                │                                │                                │
  │                                    │                                │                                ├─[31] Ledger 处理请求           │                                │
  │                                    │                                │                                │      ├─ 返回 Asset 数据        │                                │
  │                                    │                                │                                │      ├─ 返回 Position 数据    │                                │
  │                                    │                                │                                │      └─ 返回 PositionEnd      │                                │
  │                                    │                                │                                │                                │
  ├─[32] 收到 PositionEnd <─────────────────────────────────────────────────────────────────────────│
  │      └─ positions_set_ = true
  │                                    │                                │                                │                                │
  ├─[33] started_ = true              │                                │                                │                                │
  │      └─ context_->set_started(true)
  │                                    │                                │                                │                                │
  ├─[34] post_start()                 │                                │                                │                                │
  │      ├─ 注册事件处理              │                                │                                │                                │
  │      │      ├─ on_quote()         │                                │                                │                                │
  │      │      ├─ on_order()         │                                │                                │                                │
  │      │      └─ on_trade()         │                                │                                │                                │
  │      └─ invoke(&Strategy::post_start)
  │                                    │                                │                                │                                │
  └─[35] 开始接收行情和订单事件        │                                │                                │                                │
```

### prepare() 阶段详细说明

`prepare()` 函数采用**严格顺序检查**机制，每步失败则立即返回，等待下一次事件触发。

| 步骤 | 检查项 | 代码位置 | 失败处理 |
|------|--------|----------|----------|
| 1 | `has_writer(ledger_uid)` | runner.cpp:130 | 返回等待 Channel |
| 2 | 所有账户连接就绪 | runner.cpp:143 | 返回等待 |
| 3 | 发送 `BrokerStateRequest` | runner.cpp:145 | 仅发送一次 |
| 4 | 所有服务 Ready | runner.cpp:157 | 返回等待 |
| 5 | TD Channel 双向就绪 | runner.cpp:166 | 返回等待 |
| 6 | 发送 Ledger 请求 | runner.cpp:171-191 | 仅发送一次 |
| 7 | 等待 `PositionEnd` | runner.cpp:193 | 返回等待 |
| 8 | 设置 `started_ = true` | runner.cpp:200 | 完成启动 |

### 关键消息

| 步骤 | 消息类型 | 源 | 目标 | 说明 |
|------|----------|-----|------|------|
| 2 | Register(JSON) | Strategy | Master | 注册请求 |
| 6 | RequestWriteTo | Strategy | Master | 请求写入 Cached |
| 6 | RequestReadFrom | Strategy | Master | 请求读取 Cached |
| 8 | CachedReadyToRead | Master | Strategy | Cached 就绪 |
| 9 | RequestCached | Strategy | Cached | 请求缓存数据 |
| 11 | RequestCachedDone | Cached | Master | 缓存完成 |
| 12 | RequestStart | Master | Strategy | 启动信号 |
| 15 | Channel | Master | Strategy | 通道建立通知 |
| 18 | BrokerStateRequest | Strategy | Ledger | 请求 Broker 状态 |
| 20 | BrokerStateUpdate | TD | PUBLIC | Broker 状态更新 |
| 25 | KeepPositionsRequest | Strategy | Ledger | 请求保留持仓 |
| 26 | ResetBookRequest | Strategy | Ledger | 请求重置账本 |
| 27 | InstrumentKey | Strategy | Ledger | 发送合约信息 |
| 28 | RebuildPositionsRequest | Strategy | Ledger | 请求重建持仓 |
| 29 | AssetRequest | Strategy | Ledger | 请求资产数据 |
| 30 | PositionRequest | Strategy | Ledger | 请求持仓数据 |
| 31 | Asset | Ledger | Strategy | 返回资产 |
| 31 | Position | Ledger | Strategy | 返回持仓 |
| 32 | PositionEnd | Ledger | Strategy | 持仓数据结束 |

---

## 七、完整消息交互汇总

### 消息流向图

```
                         ┌──────────────┐
                         │   Master     │
                         └──────┬───────┘
                                │
           ┌────────────────────┼────────────────────┐
           │                    │                    │
           ▼                    ▼                    ▼
    ┌───────────┐        ┌───────────┐        ┌───────────┐
    │   Cached  │        │   Ledger  │        │ Strategy  │
    └─────┬─────┘        └─────┬─────┘        └─────┬─────┘
          │                   │                     │
          │   RequestCached   │                     │
          │◄──────────────────┤                     │
          │                   │                     │
          │   RequestCached   │                     │
          │◄────────────────────────────────────────┤
          │                   │                     │
          │   RequestCachedDone                     │
          ├──────────────────►│                     │
          │                   │                     │
          │   RequestCachedDone                     │
          ├────────────────────────────────────────►│
          │                   │                     │
          │                   │   AssetRequest      │
          │                   │◄────────────────────┤
          │                   │                     │
          │                   │   PositionRequest   │
          │                   │◄────────────────────┤
          │                   │                     │
          │                   │   Asset             │
          │                   ├────────────────────►│
          │                   │                     │
          │                   │   Position          │
          │                   ├────────────────────►│
          │                   │                     │
          ▼                   ▼                     ▼
```

### 所有消息类型列表

| 消息类型 | 方向 | 说明 |
|----------|------|------|
| SessionStart | Master → All | 会话开始标记 |
| Register(JSON) | App → Master | 注册请求（JSON格式） |
| Register | Master → PUBLIC | 注册确认（二进制） |
| Deregister | Master → PUBLIC | 注销通知 |
| Location | Master → PUBLIC | 位置信息 |
| Channel | Master → PUBLIC | 通道信息 |
| Band | Master → PUBLIC | Band信息 |
| TimeReset | Master → App | 时钟同步 |
| TradingDay | Master → App | 交易日 |
| RequestWriteTo | App → Master | 请求写入权限 |
| RequestReadFrom | App → Master | 请求读取权限 |
| RequestReadFromPublic | App → Master | 请求读取 PUBLIC |
| RequestReadFromSync | App → Master | 请求读取 SYNC |
| RequestWriteToBand | App → Master | 请求写入 Band |
| RequestCached | App → Cached | 请求缓存数据 |
| RequestCachedDone | Cached → Master | 缓存准备完成 |
| CachedReadyToRead | Master → App | Cached 就绪 |
| RequestStart | Master → App | 启动信号 |
| BrokerStateRequest | Strategy → Ledger | 请求 Broker 状态 |
| BrokerStateUpdate | TD → PUBLIC | Broker 状态更新 |
| KeepPositionsRequest | Strategy → Ledger | 请求保留持仓 |
| ResetBookRequest | Strategy → Ledger | 请求重置账本 |
| RebuildPositionsRequest | Strategy → Ledger | 请求重建持仓 |
| MirrorPositionsRequest | Strategy → Ledger | 请求镜像持仓 |
| AssetRequest | Strategy → Ledger | 请求资产数据 |
| PositionRequest | Strategy → Ledger | 请求持仓数据 |
| OrderTradeRequest | Strategy → TD | 请求订单成交 |
| Asset | Ledger → Strategy | 资产数据 |
| Position | Ledger → Strategy | 持仓数据 |
| PositionEnd | Ledger → Strategy | 持仓数据结束 |
| Quote | MD → Strategy | 行情数据 |
| OrderInput | Strategy → TD | 订单输入 |
| Order | TD → Strategy | 订单状态 |
| Trade | TD → Strategy | 成交数据 |
| OrderAction | Strategy → TD | 撤单请求 |

---

## 八、启动状态转换

### Apprentice 状态机

```
                    ┌─────────────────────────────────────────┐
                    │                                         │
                    ▼                                         │
    ┌───────────────────────┐     checkin()      ┌───────────────────────┐
    │     INITIALIZED       │ ─────────────────► │     REGISTERING       │
    └───────────────────────┘                    └────────────┬──────────┘
                                                             │
                                                             │ 收到 Register (self)
                                                             ▼
    ┌───────────────────────┐     RequestStart    ┌───────────────────────┐
    │     REGISTERED        │ ─────────────────► │       STARTED         │
    │ (等待 Cached 就绪)     │                    └────────────┬──────────┘
    └───────────────────────┘                                 │
                                                             │ on_start()
                                                             ▼
                                                 ┌───────────────────────┐
                                                 │     RUNNING           │
                                                 │ (业务事件循环)        │
                                                 └───────────────────────┘
```

### Strategy 状态机（额外阶段）

```
                    ┌─────────────────────────────────────────┐
                    │                                         │
                    ▼                                         │
    ┌───────────────────────┐     pre_start()     ┌───────────────────────┐
    │      STARTED          │ ─────────────────► │     PREPARING         │
    └───────────────────────┘                    └────────────┬──────────┘
                                                             │
                                                             │ prepare() 完成
                                                             │ - 所有账户连接
                                                             │ - 所有服务 Ready
                                                             │ - 持仓数据就绪
                                                             ▼
    ┌───────────────────────┐     post_start()    ┌───────────────────────┐
    │     POSITIONS_SET     │ ─────────────────► │      RUNNING          │
    └───────────────────────┘                    └───────────────────────┘
```

---

## 九、关键设计要点

### 1. 启动协调机制

- **Master 作为协调中心**：所有服务必须先注册到 Master
- **Cached 作为数据中心**：所有服务启动时需要从 Cached 恢复状态
- **RequestStart 作为启动信号**：Master 在收到 `RequestCachedDone` 后才发送启动信号

### 2. 依赖关系

```
Strategy
  ├─ 依赖 MD (行情数据)
  ├─ 依赖 TD (交易执行)
  ├─ 依赖 Ledger (资产/持仓)
  └─ 依赖 Cached (状态恢复)

TD
  └─ 依赖 Cached (状态恢复)

MD
  └─ 依赖 Cached (状态恢复)

Ledger
  └─ 依赖 Cached (状态恢复)

Cached
  └─ 无外部依赖
```

### 3. 容错设计

- **注册超时**：Apprentice 在 60 秒内未收到注册确认会自动退出
- **连接检查**：Strategy 的 `prepare()` 会检查所有依赖服务是否就绪
- **状态恢复**：通过 Cached 实现崩溃恢复

### 4. 低延迟优化

- **直接通道**：服务之间通过 Channel 直接通信，不经过 Master
- **内存映射文件**：使用 journal 实现高效的进程间通信
- **事件驱动**：基于 RxCpp 实现响应式编程