# FAQs


kungfu-cpp\apps\strategies\strategy101.h  strategy是怎么实现和 MD 交互的？

## Strategy 与 MD 交互机制

Strategy 与 MD 之间**没有任何网络通信**，全部依赖**内存映射 journal 文件**（mmap IPC）。整个交互分为两个方向：**Strategy → MD（订阅请求）** 和 **MD → Strategy（行情推送）**。

---

### 一、Strategy → MD：订阅请求链路

入口在 [strategy101.h:27](../apps/strategies/strategy101.h#L27)：

```cpp
context->subscribe("sim", {"600000"}, {"SSE"});
```

#### 1. `RuntimeContext::subscribe` ([runtime.cpp:69-78](../src/wingchun/strategy/runtime.cpp#L69-L78))

```cpp
void RuntimeContext::subscribe(const std::string &source, const std::vector<std::string> &instrument_ids,
                               const std::string &exchange_ids) {
  auto md_location = find_md_location(source);          // 定位 MD 进程
  for (const auto &instrument_id : instrument_ids) {
    broker_client_.subscribe(md_location, exchange_ids, instrument_id);
  }
  md_locations_.emplace(md_location->uid, md_location);
  ensure_connect();        // 建立与 MD 的 journal 通道
  send_instrument_keys();  // 把订阅的 InstrumentKey 写到 MD 的 journal
}
```

#### 2. `find_md_location` ([runtime.cpp:303-313](../src/wingchun/strategy/runtime.cpp#L303-L313))

构造 MD 的 location 标识：`mode::LIVE, category::MD, "sim", "sim"`，并通过 `app_.has_location(md_location->uid)` 校验 MD 是否已注册。

#### 3. `PassiveClient::subscribe` → `Client::subscribe` ([client.cpp:98-103](../src/wingchun/broker/client.cpp#L98-L103))

这里**并不立刻发消息给 MD**，只是在 client 本地登记：
- `instrument_keys_`：保存 `InstrumentKey`（exchange+instrument 的 hash）
- `exchange_md_locations_` / `instrument_md_locations_`：建立"哪个合约由哪个 MD 提供"的反查表

#### 4. `ensure_connect` ([runtime.cpp:355-370](../src/wingchun/strategy/runtime.cpp#L355-L370))

遍历所有已注册的 broker（来自 `app_.get_registry()` / `app_.get_bands()`），调用 `Client::connect`（[client.cpp:152-174](../src/wingchun/broker/client.cpp#L152-L174)）。对 MD 类型，关键的两步是：

```cpp
app_.request_write_to(app_.now(), app_uid);              // 请求开 MD→Strategy 的写通道
app_.request_read_from_public(app_.now(), app_uid, ...); // 请求读 MD 的 PUBLIC journal
```

这两个 `request_*` 不是直接写数据，而是向** master/ledger 进程**发 channel 建立请求。Ledger 收到后会建立从 strategy 到 MD 的 journal 链路（双方通过 `inspect_channel` 在 [runner.cpp:48-58](../src/wingchun/strategy/runner.cpp#L48-L58) 中确认并 `reader_->join` 加入 reader）。

#### 5. `send_instrument_keys` → `try_renew` → `renew` ([client.cpp:105-128](../src/wingchun/broker/client.cpp#L105-L128))

```cpp
void Client::renew(int64_t trigger_time, const location_ptr &md_location) {
  auto writer = app_.get_writer(md_location->uid);  // 拿到指向 MD 的 writer
  for (const auto &pair : instrument_keys_) {
    // ...找到这个 instrument 对应的 md_location...
    if (source_location and md_location->uid == source_location->uid) {
      writer->write(trigger_time, instrument_key);   // ★ 把 InstrumentKey 写到 MD 的 journal
    }
  }
}
```

**这是真正发给 MD 的订阅请求**——把 `InstrumentKey`（exchange_id + instrument_id）作为一条 frame 写入 MD 进程会读的 journal。MD 拿到 InstrumentKey 后，便开始向上游行情源订阅并把 `Quote` 帧写回自己的 PUBLIC journal。

注意 `try_renew` 有一个守卫：`ready_md_locations_` 里没有这个 MD 时直接返回 false（[client.cpp:122-128](../src/wingchun/broker/client.cpp#L122-L128)）。所以 `pre_start` 阶段调用 `subscribe` 时，如果 MD 还没就绪，订阅请求会推迟；MD Ready 之后由 `update_broker_state`（[client.cpp:187-212](../src/wingchun/broker/client.cpp#L187-L212)）触发一次 `renew`，再由 `prepare` 流程（[runner.cpp:121-203](../src/wingchun/strategy/runner.cpp#L121-L203)）中 `writer->write(now, pair.second)` 再次把所有 InstrumentKey 重发给 ledger/MD。

---

### 二、MD → Strategy：行情推送链路

#### 1. journal 帧轮询

MD 把 `Quote` 写到自己的 PUBLIC journal 文件后，Strategy 进程内的 `apprentice` 通过 **libuv worker 线程** 调用 `reader` 的 `step()` 持续轮询所有已 join 的 journal（包括 MD 的 PUBLIC），读到新帧就把它投递到主线程的 `events_` RxCpp observable。

#### 2. 事件路由注册（关键）

在 [runner.cpp:81-82](../src/wingchun/strategy/runner.cpp#L81-L82)：

```cpp
events_ | is_own<Quote>(context_->get_broker_client()) |
    $$(invoke(&Strategy::on_quote, event->data<Quote>(), get_location(event->source())));
```

`is_own<Quote>` ([client.h:258-288](../include/kungfu/wingchun/broker/client.h#L258-L288)) 是一个 RxCpp filter，它判断这一帧 Quote 是否应该交给本 strategy：

```cpp
return rx::filter([&](const event_ptr &event) {
  if (event->msg_type() == DataType::tag) {
    const DataType &data = event->data<DataType>();
    if (broker_client.is_custom_subscribed(event->source())) {  // subscribe_all 命中？
      if (... is_custom_subscribed_all(... Snapshot, data.exchange_id, data.instrument_type)) {
        return true;
      }
    }
    if (broker_client.is_subscribed(data.exchange_id, data.instrument_id)) {  // 精确订阅命中？
      return true;
    }
  }
  return false;
});
```

即：**MD 广播到 PUBLIC journal 的所有 Quote，Strategy 这里按 exchange_id+instrument_id 过滤，只有订阅过的合约才会触发回调**。

#### 3. 触发 `on_quote`

匹配后，`invoke(&Strategy::on_quote, ...)` 反射调用 [strategy101.h:89](../apps/strategies/strategy101.h#L89)：

```cpp
void on_quote(Context_ptr &context, const longfist::types::Quote &quote,
              const kungfu::yijinjing::data::location_ptr &location) override {
  SPDLOG_INFO("on quote: {} i {} location->uid {}", quote.last_price, i, location->location_uid);
  i++;
  if (i % 10 == 0) {
    context->insert_order("600000", "SSE", "sim", "sim", quote.last_price, 100,
                          PriceType::Limit, Side::Buy, Offset::Open);
  }
}
```

收到行情后，strategy 又通过 `context->insert_order(...)`（[runtime.cpp:106-145](../src/wingchun/strategy/runtime.cpp#L106-L145)）把 `OrderInput` 写到 TD 的 journal——这就是反向链路（Strategy → TD），机制完全一样：writer 写 journal，TD 进程读 journal。

---

### 三、`post_start` 里的"直接读 MD journal"

[strategy101.h:50-67](../apps/strategies/strategy101.h#L50-L67) 还演示了**绕过事件系统、直接读取 MD journal** 的方式：

```cpp
auto l_ptr = location::make_shared(mode::LIVE, category::MD, "sim", "sim", std::make_shared<locator>());
kungfu::yijinjing::journal::assemble asb(l_ptr, location::PUBLIC, AssembleMode::All);
auto headers = asb.read_headers(Location{});
auto locations = asb2.read_bytes<Location>();
auto l3 = asb3.read_all<Location>();
```

`journal::assemble` 直接 mmap MD 的 PUBLIC journal 文件，读取历史帧（这里读的是 `Location` 类型的帧，用于发现系统中有哪些 location）。这是**一次性、同步、按需读取**的方式，区别于上面的"注册回调、持续推送"模式。它适用于启动时做状态恢复/发现，不适合实时行情处理。

---

### 四、总结时序

```
Strategy                           Ledger/Master                    MD 进程
   │                                    │                              │
   │ subscribe("sim",{"600000"},"SSE")  │                              │
   │ ├─► PassiveClient.subscribe 本地登记 instrument_keys              │
   │ ├─► ensure_connect()                                              │
   │ │     request_write_to(md_uid) ───►│ 建立 strategy↔md channel     │
   │ │     request_read_from_public ───►│                              │
   │ ├─► send_instrument_keys()         │                              │
   │ │     try_renew → renew:           │                              │
   │ │     writer.write(InstrumentKey) ────────────► journal ─────────►│ 收到订阅
   │ │                                                                │ 上游订阅
   │ │                                                                │ 写 Quote
   │ │◄─────────── reader.step() 轮询 ◄─────────── journal ◄──────────│ 到 PUBLIC
   │ │                                                                │
   │ │ events_ | is_own<Quote> | invoke(on_quote)                     │
   │ │ ──► on_quote(context, quote, md_location)                      │
   │ │      └─► context->insert_order(...) ──► TD journal ──► ...     │
```

核心要点：
- **唯一 IPC 介质**：内存映射 journal 文件，无 socket / 无 RPC
- **请求方向**（Strategy→MD）：写 `InstrumentKey` 帧到 MD 的 journal
- **数据方向**（MD→Strategy）：MD 写 Quote 到 PUBLIC journal，Strategy 的 reader 轮询读取，经 RxCpp `events_` 流通过 `is_own<Quote>` 过滤后回调 `on_quote`
- **通道建立**：通过向 ledger/master 发 `request_write_to` / `request_read_from_public` 完成
- **就绪守卫**：MD 必须 `BrokerState::Ready` 后 `try_renew` 才真正下发订阅；MD 重连时会自动重新 `renew`，保证订阅不丢

---

PUBLIC journal 支持多个消费者同时读取吗

## 是的，PUBLIC journal 原生支持多个消费者同时读取

这是一个典型的**单写多读**（Single-Writer / Multi-Reader）广播模式，设计上就支持任意数量的并发消费者，且消费者之间互不干扰。下面从四个层面说明。

---

### 一、共享内存里只存"写位置"，不存"读位置"

[page.h:11-18](../include/kungfu/yijinjing/journal/page.h#L11-L18) 里 page_header 的定义（被注释但完整可见）：

```cpp
KF_DEFINE_PACK_TYPE(page_header, 1, PK(version), PERPETUAL(),
    (uint32_t, version),
    (uint32_t, page_header_length),
    (uint32_t, page_size),
    (uint32_t, frame_header_length),
    (uint64_t, last_frame_position)   // ← 只有"写头"，没有任何 reader 游标
);
```

`last_frame_position` 是 writer 写到哪里的进度，是 mmap 共享内存里**唯一**的位置状态。Reader 的游标**根本不存在共享内存里**，所以多个 reader 之间没有共享状态需要协调——这是并发读的基础。

---

### 二、每个 reader 在自己的进程内存里维护独立游标

[journal.h:50-56](../include/kungfu/yijinjing/journal/journal.h#L50-L56) 中 `journal` 类的成员：

```cpp
private:
  const data::location_ptr location_;
  const uint32_t dest_id_;
  page_ptr page_;            // 当前 mmap 的 page
  frame_ptr frame_;          // ← 当前帧指针（进程内存中的对象）
  uint64_t page_frame_nb_;   // ← 当前页内的帧序号（进程内存）
```

[journal.h:102-105](../include/kungfu/yijinjing/journal/journal.h#L102-L105) 中 `reader` 类：

```cpp
private:
  const bool lazy_;
  journal *current_;
  std::unordered_map<uint64_t, journal> journals_;  // ← 每个 (location,dest) 一份独立 journal
```

[journal.cpp:17-25](../src/yijinjing/journal/journal.cpp#L17-L25) 中 `next()` 推进游标：

```cpp
void journal::next() {
  assert(page_.get() != nullptr);
  if (frame_->msg_type() == longfist::types::PageEnd::tag) {
    load_next_page();
  } else {
    frame_->move_to_next();   // ← 只改本进程内存里的 frame_ 指针
    page_frame_nb_++;
  }
}
```

**关键点**：`frame_->move_to_next()` 只修改 reader 自己持有的 `frame` 对象里的指针，**完全不触碰 mmap 共享内存**。所以 N 个 reader 各自推进各自的游标，互不可见、互不干扰。

---

### 三、reader 以只读方式 mmap，writer 的更新对所有 reader 可见

[mmap.cpp:27-97](../src/yijinjing/util/mmap.cpp#L27-L97) 里 `load_mmap_buffer` 根据 `is_writing` 选择保护属性：

| 平台 | Reader (`is_writing=false, lazy=true`) | Writer (`is_writing=true`) |
|------|----------------------------------------|----------------------------|
| Linux | `O_RDONLY` + `PROT_READ` + `MAP_SHARED` | `O_RDWR` + `PROT_READ\|PROT_WRITE` + `MAP_SHARED` |
| Windows | `GENERIC_READ` + `PAGE_READONLY` + `FILE_MAP_READ` | `GENERIC_READ\|WRITE` + `PAGE_READWRITE` + `FILE_MAP_ALL_ACCESS` |

注意两点：

1. **`MAP_SHARED`（Windows 的 named file mapping）** 保证 writer 对 `last_frame_position` 和帧数据的写入，通过 OS 页缓存**立即可见于所有 reader 进程**——这正是 PUBLIC journal 能"广播"的物理基础。
2. **Reader 映射是只读的**，物理上无法写共享内存，因此即使代码出 bug 也不会污染 journal。`page::set_last_frame_position` 是 writer 私有方法（[page.cpp:23-25](../src/yijinjing/journal/page.cpp#L23-L25)），reader 拿不到写权限。

---

### 四、Reader 如何"感知"新数据：轮询帧头，无需锁

[reader.cpp:47-50](../src/yijinjing/journal/reader.cpp#L47-L50) 和 [reader.cpp:66-76](../src/yijinjing/journal/reader.cpp#L66-L76)：

```cpp
bool reader::data_available() {
  sort();
  return current_ != nullptr && current_frame()->has_data();
}

void reader::sort() {
  int64_t min_time = time::now_in_nano();
  for (auto &pair : journals_) {
    auto &journal = pair.second;
    auto &frame = journal.current_frame();
    if (frame->has_data() && frame->gen_time() <= min_time) {  // ← 读帧头的 gen_time
      min_time = frame->gen_time();
      current_ = &journal;
    }
  }
}
```

Reader 通过读帧头里的 `gen_time` / `msg_type` / `length` 字段判断当前帧是否有效（`has_data()`）。这些字段由 writer 在 `close_frame` 时一次性写好（[journal.h:124](../include/kungfu/yijinjing/journal/journal.h#L124)）。Reader 看到合法的 `gen_time` 就认为这帧可读。

- **同一进程内** writer 用 `std::mutex writer_mtx_`（[journal.h:206](../include/kungfu/yijinjing/journal/journal.h#L206)）保证 `open_frame`/`close_frame` 不会被并发打断
- **跨进程**靠 `last_frame_position` 这一 64 位字段（在 64 位系统上写操作原子）+ `close_frame` 先填帧数据再更新 position 的顺序约定，保证 reader 不会读到半成品帧
- **没有任何 fcntl 文件锁、信号量、条件变量**——典型的无锁 SPSC 广播

---

### 五、`reader::join` 是 per-reader 的订阅

[reader.cpp:10-19](../src/yijinjing/journal/reader.cpp#L10-L19)：

```cpp
void reader::join(const data::location_ptr &location, uint32_t dest_id, const int64_t from_time) {
  auto key = static_cast<uint64_t>(location->uid) << 32u | static_cast<uint64_t>(dest_id);
  auto result = journals_.try_emplace(key, location, dest_id, false, lazy_);
  if (result.second) {
    journals_.at(key).seek_to_time(from_time);   // ← 每个 reader 独立决定从哪里开始读
  }
  ...
}
```

每个 reader 实例：
- 可以独立 `join` 同一个 (location, PUBLIC) 而不影响别人
- 可以独立 `seek_to_time(from_time)` 选择从任意时间点开始消费（重放历史 / 跳过旧数据）
- 可以独立 `disjoin` 退出

所以 strategy101.h 里 [strategy101.h:50-67](../apps/strategies/strategy101.h#L50-L67) 直接 `new` 出来的 `assemble` 实例，可以和 apprentice 内部的 `reader_` 同时读 MD 的 PUBLIC journal，互不影响——这是该示例代码能成立的前提。

---

### 六、多消费者场景的总结

| 维度 | 表现 |
|------|------|
| **跨进程多 reader** | ✅ 每个 reader 进程独立 mmap 同一个 journal 文件（`MAP_SHARED` 只读），OS 页缓存统一可见 |
| **同进程多 reader** | ✅ 每个 `reader` 对象有独立的 `journals_` map 和 `current_`，各自 `join`、各自 `next` |
| **读位置隔离** | ✅ 游标在 reader/journal 的 C++ 对象里（堆内存），共享内存中只有写头 |
| **历史回放** | ✅ 每个 reader 可独立 `seek_to_time` 从任意时间点重读 |
| **广播语义** | ✅ 一条帧写入后，所有已 join 的 reader 都会读到（除非主动 disjoin） |
| **writer 阻塞 reader？** | ❌ 不阻塞，reader 轮询读 `gen_time`，writer 不感知 reader 数量 |
| **reader 阻塞 writer？** | ❌ 不阻塞，writer 只关心 `last_frame_position` 是否到 page 边界，与 reader 无关 |
| **并发上限** | 无显式限制，受 OS 文件句柄 / mmap 区域 / 页缓存压力约束 |

**一句话**：PUBLIC journal 是一种"写入即广播、读取即轮询"的无锁共享内存环形文件，单 writer 串行追加，任意数量 reader 各持私有游标并发消费，是 kungfu 跨进程行情/订单分发的基础设施。

---

location::PUBLIC  和 location::SYNC 的区别

## `location::PUBLIC` vs `location::SYNC` 的区别

这两个常量定义在 [common.h:102-103](../include/kungfu/yijinjing/common.h#L102-L103)：

```cpp
struct location : ... {
  static constexpr uint32_t PUBLIC = 0;  // 广播通道
  static constexpr uint32_t SYNC = 1;    // 同步通道
  ...
};
```

它们是 **`dest_id`（目标通道 ID）**，不是 location 的种类，而是 journal 路由时的"目的端口号"。每个 location 进程都有**多条独立的 journal 文件**，按 `dest_id` 区分，文件名形如 `{dest_id:08x}.{page_id}.journal`（见 [page.cpp:67-70](../src/yijinjing/journal/page.cpp#L67-L70)）。

除了 `PUBLIC=0` 和 `SYNC=1`，还有第三类：**私有通道** `dest_id = 某个 app 的 uid`（点对点）。

---

### 一、三者的本质区别

| 维度 | `PUBLIC` (=0) | `SYNC` (=1) | 私有通道 (=app_uid) |
|------|---------------|-------------|----------------------|
| **语义** | 广播：所有已 join 的进程都能读 | 同步：用于"初始状态快照"分发 | 点对点：定向发给某个进程 |
| **典型数据** | Quote / Order / Trade / BrokerStateUpdate / Channel / Location / Deregister | Asset 快照 / Position 快照 / PositionEnd / AssetMargin 快照 | OrderInput（strategy→TD）/ AssetRequest,PositionRequest（请求）/ 私有回执 |
| **生命周期** | 持续追加，进程整个生命周期 | 主要在启动/重连阶段，状态同步用 | 全程，定向通信 |
| **page 大小**（[page.h:84-94](../include/kungfu/yijinjing/journal/page.h#L84-L94)） | MD=128MB，TD/STRATEGY=1MB，其它=1MB | MD=1MB，TD/STRATEGY=16MB，其它=1MB | TD/STRATEGY=16MB，MD=1MB |
| **读法** | `request_read_from_public` | `request_read_from_sync` | `request_read_from` |

注意 `find_page_size` 里的判断是反的：
```cpp
if (location->category == MD && dest_id != 1) return 128 * MB;  // MD 非 SYNC（即 PUBLIC）→ 128MB
if ((TD||STRATEGY) && dest_id != 0) return 16 * MB;              // TD/STRATEGY 非 PUBLIC（即 SYNC 或私有）→ 16MB
return MB;
```
所以 **MD 的 PUBLIC journal 用 128MB 大页**（行情数据量大），**TD 的 SYNC/私有 journal 用 16MB 中页**（订单/持仓中等量），其余 1MB 小页。

---

### 二、读写 API 的分叉

[apprentice.cpp:249-253](../src/yijinjing/practice/apprentice.cpp#L249-L253) 三个 handler 用不同 `dest_id` 调 `do_read_from`：

```cpp
void apprentice::on_read_from(const event_ptr &event) {
  do_read_from<RequestReadFrom>(event, get_live_home_uid());   // 私有：dest_id = 自己的 uid
}
void apprentice::on_read_from_public(const event_ptr &event) {
  do_read_from<RequestReadFromPublic>(event, 0);               // PUBLIC：dest_id = 0
}
void apprentice::on_read_from_sync(const event_ptr &event) {
  do_read_from<RequestReadFromSync>(event, location::SYNC);    // SYNC：dest_id = 1
}
```

`do_read_from` 模板（[apprentice.h:191-194](../include/kungfu/yijinjing/practice/apprentice.h#L191-L194)）：

```cpp
template <typename DataType> void do_read_from(const event_ptr &event, uint32_t dest_id) {
  const DataType &request = event->data<DataType>();
  reader_->join(get_location(request.source_id), dest_id, request.from_time);  // 按 dest_id 加入对应 journal
}
```

所以 `request_read_from_public/sync` 在源进程里最终让 reader join 到源 location 的 `{PUBLIC|SYNC}.journal` 文件，与其它 dest_id 的 journal 完全隔离。

---

### 三、谁连谁：连接拓扑

回顾 [client.cpp:152-174](../src/wingchun/broker/client.cpp#L152-L174) `Client::connect`，不同 category 连的通道不同：

```cpp
if (MD) {
  app_.request_write_to(now, app_uid);              // 让 MD 给 strategy 开写通道（私有）
  app_.request_read_from_public(now, app_uid, ...); // strategy 读 MD 的 PUBLIC journal（行情）
}
if (TD) {
  app_.request_write_to(now, app_uid);              // 让 TD 给 strategy 开写通道（私有，下单）
  app_.request_read_from(now, app_uid, ...);        // strategy 读 TD 给自己的私有 journal（订单回报）
  app_.request_read_from_public(now, app_uid, ...); // strategy 读 TD 的 PUBLIC journal（BrokerStateUpdate 等）
  app_.request_read_from_sync(now, app_uid, ...);   // ★ strategy 读 TD 的 SYNC journal（持仓/资金快照）
}
```

**关键观察**：
- **MD 只开 PUBLIC**——行情天然是广播，没有 SYNC 概念
- **TD 三个全开**——PUBLIC 用于状态广播，SYNC 用于持仓/资金初始同步，私有用于订单回报
- master 在 [master.cpp:130-132](../src/yijinjing/practice/master.cpp#L130-L132) 给每个新注册的 app 都默认开 PUBLIC + SYNC + master_cmd 三套

---

### 四、SYNC 通道的核心用途：状态快照同步

[bookkeeper.cpp:73-76](../src/wingchun/book/bookkeeper.cpp#L73-L76) 揭示了 SYNC 的真正语义：

```cpp
events | fork<Asset>(location::SYNC, &Bookkeeper::try_sync_asset, &Bookkeeper::try_update_asset);
events | fork<AssetMargin>(location::SYNC, &Bookkeeper::try_sync_asset_margin, &Bookkeeper::try_update_asset_margin);
events | fork<Position>(location::SYNC, &Bookkeeper::try_sync_position, &Bookkeeper::try_update_position);
events | fork<PositionEnd>(location::SYNC, &Bookkeeper::try_sync_position_end, &Bookkeeper::try_update_position_end);
```

`fork<T>(dest, sync_handler, update_handler)` 的实现（[bookkeeper.h:108-123](../include/kungfu/wingchun/book/bookkeeper.h#L108-L123)）：

```cpp
template <typename T, ...> constexpr decltype(auto) fork(uint32_t dest, RouteA t1, RouteB t2) {
  return kungfu::rx::$([&, dest, t1, t2](const event_ptr &event) {
    if (event->msg_type() != T::tag) return;
    if (event->dest() == dest) {            // ★ dest == SYNC
      (this->*t1)(data);                    // → try_sync_*（全量快照覆盖）
    } else {                                // dest == PUBLIC 或私有
      (this->*t2)(data);                    // → try_update_*（增量更新）
    }
  });
}
```

**同一种数据类型（Asset/Position）会出现在两条 journal 上**：
- 写到 **SYNC** 的：表示"这是初始/重建时的全量快照"，ledger/bookkeeper 用 `try_sync_*` **整本账册替换**
- 写到 **PUBLIC** 的：表示"这是交易过程中的增量变化"，用 `try_update_*` **在现有账册上累加修改**

TD 端由 `Trader::get_asset_writer()` 等决定写哪条（[trader.cpp:98-108](../src/wingchun/broker/trader.cpp#L98-L108)）：

```cpp
yijinjing::journal::writer_ptr Trader::get_asset_writer() const {
  return get_writer(sync_asset_ ? location::SYNC : location::PUBLIC);
}
yijinjing::journal::writer_ptr Trader::get_position_writer() const {
  return get_writer(sync_position_ ? location::SYNC : location::PUBLIC);
}
```

`sync_asset_` / `sync_position_` 这些标志由 `enable_asset_sync()` 等接口在启动时设置——TD 启动时先写 SYNC 通道下发快照，之后切回 PUBLIC 通道做增量广播。

---

### 五、PUBLIC 通道的核心用途：广播

[master.cpp](../src/yijinjing/practice/master.cpp) 大量用 `get_writer(location::PUBLIC)` 广播全局事件，比如：

- `Location`（新进程上线，[master.cpp:127](../src/yijinjing/practice/master.cpp#L127)）
- `Register`（注册信息）
- `Channel`（通道建立通知，[master.cpp:296,314](../src/yijinjing/practice/master.cpp#L296)）
- `Band`（频段建立）
- `Deregister`（进程下线，[master.cpp:158](../src/yijinjing/practice/master.cpp#L158)）
- `TradingDay`（交易日切换）

[broker.cpp:116-123](../src/wingchun/broker/broker.cpp#L116-L123) TD 自己的 `BrokerStateUpdate` 也写 PUBLIC：

```cpp
void BrokerService::update_broker_state(BrokerState state) {
  auto writer = get_writer(location::PUBLIC);
  BrokerStateUpdate &broker_state = writer->open_data<BrokerStateUpdate>();
  broker_state.state = state_;
  broker_state.location_uid = get_home_uid();
  writer->close_data();
}
```

而 strategy101.h 里 [on_broker_state_change](../apps/strategies/strategy101.h#L113) 收到的就是这些从 TD/MD PUBLIC journal 广播来的 `BrokerStateUpdate`。

---

### 六、strategy101.h 里 `assemble` 读 PUBLIC 的例子

[strategy101.h:50-67](../apps/strategies/strategy101.h#L50-L67)：

```cpp
auto l_ptr = location::make_shared(mode::LIVE, category::MD, "sim", "sim", std::make_shared<locator>());
kungfu::yijinjing::journal::assemble asb(l_ptr, location::PUBLIC, AssembleMode::All);
auto headers = asb.read_headers(Location{});
auto locations = asb2.read_bytes<Location>();
```

这里 `assemble` 显式传 `location::PUBLIC`，即去读 MD 进程的 `00000001.0.journal`（dest_id=0 的文件）。如果想读 SYNC 通道，就传 `location::SYNC`，对应文件名 `00000001.0.journal` → 实际上是 `00000001.{page}.journal`（dest_id 占高 8 位十六进制，PUBLIC=`00000000`，SYNC=`00000001`）。

---

### 七、一句话总结

- **`PUBLIC` (=0)**：**广播通道**，所有进程都读同一份 journal 文件，承载持续性的、可被多方消费的数据（行情、订单、状态变更、系统元信息）。多读者天然支持。
- **`SYNC` (=1)**：**同步通道**，承载状态快照（Asset / Position / AssetMargin / PositionEnd），用于进程启动/重连时把账册**整体重建**。同样的数据类型，写到 SYNC 还是 PUBLIC 决定了 bookkeeper 是 `try_sync_*`（覆盖式）还是 `try_update_*`（增量式）。
- **私有通道（=app_uid）**：点对点定向通信（下单、私有回报、请求-响应）。

三者**物理上是三个不同的 mmap journal 文件**，通过 `dest_id` 区分，由 master 在 app 注册时统一为每个 app 开通（[master.cpp:130-132](../src/yijinjing/practice/master.cpp#L130-L132)），由 reader 通过 `join(location, dest_id, from_time)` 选择性订阅。

---

这里的 Band 是什么含义？ RequestWriteToBand 是做什么的？

## `Band` 是什么含义？

`Band` 是 kungfu 框架里的一种**命名订阅组**（named topic / publish-subscribe group）机制，比 `Channel` 更“柔性”。它允许一个 MD 类型的发布者在自己进程的 journal 里**开多条独立的命名子通道**，让已经订阅该 MD 源的策略**自动 join 进来**读取。

### 一、类型定义对比

[types.h:211-236](../include/kungfu/longfist/types.h#L211-L236)：

```cpp
// 点对点通道：dest_id 是某个真实 app 的 uid
KF_DEFINE_PACK_TYPE(Channel, 10028, PK(source_id, dest_id), PERPETUAL(),
    (uint32_t, source_id),
    (uint32_t, dest_id));

// 请求建一个 Band：携带一个完整的虚拟 Location（mode/category/group/name）
KF_DEFINE_DATA_TYPE(RequestWriteToBand, 10032, PK(location_uid), PERPETUAL(),
    (uint32_t, location_uid),
    (enums::category, category),
    (enums::mode, mode),
    (std::string, group),
    (std::string, name));

// Band 注册通知：source_id=发布者 uid，dest_id=虚拟 band location 的 uid
KF_DEFINE_PACK_TYPE(Band, 10033, PK(source_id, dest_id), PERPETUAL(),
    (uint32_t, source_id),
    (uint32_t, dest_id));
```

**关键差别**：
- `Channel`：`dest_id` 是某个真实 app 的 uid → 点对点定向通信（strategy↔TD 私有订单回报）
- `Band`：`dest_id` 是一个**虚拟 location 的 uid**（由 band 名 hash 出来），物理上没有对应进程，只是一个“订阅组名” → 多消费者可同时 join 这同一个 dest_id

二者物理上都是 journal 文件里的 dest_id，文件名 `{dest_id:08x}.{page}.journal`，但语义不同。

---

### 二、`RequestWriteToBand` 做了什么：完整流程

#### 1. 发布者侧：`apprentice::request_band` ([apprentice.cpp:75-81](../src/yijinjing/practice/apprentice.cpp#L75-L81))

```cpp
uint32_t apprentice::request_band(const std::string &band_name) {
  auto io_device = get_io_device();
  auto home = io_device->get_home();
  // ★ 用“自己 mode/category/group + band_name”构造一个虚拟 location
  auto band_location = location::make_shared(home->mode, home->category, home->group, band_name, get_locator());
  request_write_to_band(now(), band_location);   // 发请求给 master
  return band_location->uid;                      // 返回 band 的 dest_id，发布者后续用它写数据
}
```

**重点**：band_location 复用调用者自己的 `mode/category/group`，只把 `name` 换成 `band_name`。比如一个注册为 `MD/bar/bar` 的 bar 生成器调用 `request_band("my-bar")`，就得到虚拟 location `MD/bar/my-bar`，其 uid 是该字符串的 hash，作为后续 journal 的 dest_id。

`request_write_to_band` ([hero.cpp:323-329](../src/yijinjing/practice/hero.cpp#L323-L329)) 把 `RequestWriteToBand` 消息写到 master_cmd 通道。

#### 2. master 侧：`on_request_write_to_band` ([master.cpp:255-277](../src/yijinjing/practice/master.cpp#L255-L277))

```cpp
void master::on_request_write_to_band(const event_ptr &event) {
  const RequestWriteToBand &request = event->data<RequestWriteToBand>();
  auto app_uid = event->source();
  auto target_location = location::make_shared(request, home->locator);  // 重建虚拟 band location

  // ① 把这个虚拟 location 通知给所有人（注意：它没有真实进程，仅是个“名字”）
  try_add_location(now(), target_location);
  get_writer(location::PUBLIC)->write(now(), dynamic_cast<Location &>(*target_location));

  // ② master 自己开始读发布者 journal 的 dest_id=band_uid 这条通道
  reader_->join(get_location(app_uid), request.location_uid, trigger_time);

  // ③ 让发布者开 writer 到 band_uid（发布者之后用 get_writer(band_uid) 写数据）
  require_write_to_band(trigger_time, app_uid, target_location);

  // ④ 构造 Band{source=app_uid, dest=band_uid} 广播到 PUBLIC，通知所有订阅者
  Band band = {};
  band.source_id = app_uid;
  band.dest_id = target_location->location_uid;
  register_band(trigger_time, band);
  get_writer(location::PUBLIC)->write(trigger_time, band);
}
```

`require_write_to_band` ([hero.cpp:323-329](../src/yijinjing/practice/hero.cpp#L323-L329)) 会把 `RequestWriteTo` 发给发布者，让发布者的 `on_write_to` 在 `writers_` map 里 emplace 一个 `band_uid` 的 writer。从此发布者可以用 `get_writer(band_uid)` 把数据写到这条独立的 journal 文件。

#### 3. 订阅者侧：自动 join

[client.cpp:145-150](../src/wingchun/broker/client.cpp#L145-L150) 注册了 Band 事件的处理：

```cpp
void Client::on_start(const rx::connectable_observable<event_ptr> &events) {
  events | is(Register::tag)     | $$(connect(event, event->data<Register>()));
  events | is(Band::tag)         | $$(connect(event, event->data<Band>()));   // ★ 收到 Band 广播
  events | is(BrokerStateUpdate::tag) | $$(update_broker_state(...));
  ...
}
```

[client.cpp:176-185](../src/wingchun/broker/client.cpp#L176-L185) `connect(event, Band)`：

```cpp
void Client::connect(const event_ptr &event, const Band &band) {
  auto source_id = band.source_id;     // 发布者 uid
  auto dest_id = band.dest_id;         // band 虚拟 location uid
  auto source_location = app_.get_location(source_id);
  if (source_location->category == category::MD and should_connect_md(source_location)) {
    app_.request_read_from_source_to_dest(event->gen_time(), source_location, dest_id);
    // ↑ reader_->join(source_location, dest_id, time)  ← 加入发布者 journal 的 band_uid 通道
  }
}
```

`should_connect_md` 在 `PassiveClient` 里的实现（[client.cpp:379-385](../src/wingchun/broker/client.cpp#L379-L385)）：

```cpp
bool PassiveClient::should_connect_md(const location_ptr &md_location) const {
  return enrolled_md_locations_.find(md_location->uid) != enrolled_md_locations_.end();
}
```

`enrolled_md_locations_` 是策略调用 `context->subscribe(source, ...)` 时通过 `PassiveClient::subscribe` ([client.cpp:339-345](../src/wingchun/broker/client.cpp#L339-L345)) 登记的。

**所以 Band 是自动 join 的**：策略只要 `subscribe("bar", ...)` 登记过 "bar" 这个 MD 源，那么 "bar" 源下任何 app 创建的所有 Band 都会被这个策略自动 join，无需显式订阅每个 band。

---

### 三、三种 dest_id 通道的对比

| 维度 | `PUBLIC` (=0) | `SYNC` (=1) | 私有 Channel (=app_uid) | **Band** (=虚拟 location uid) |
|------|---------------|-------------|--------------------------|-------------------------------|
| **语义** | 全局广播 | 状态快照同步 | 点对点定向 | **命名订阅组**（MD 内的子通道） |
| **谁写** | 任何 app | TD（快照阶段） | 任何 app 给特定对象写 | **MD 类发布者**用 `get_writer(band_uid)` |
| **谁读** | 所有 join PUBLIC 的 app | 所有 join SYNC 的 app | 仅 dest_id 对应的那个 app | 所有 enrolled 该 MD 源的策略，自动 join |
| **建立方式** | 注册时 master 自动开 ([master.cpp:130](../src/yijinjing/practice/master.cpp#L130)) | 注册时 master 自动开 ([master.cpp:131](../src/yijinjing/practice/master.cpp#L131)) | `request_read_from` / `request_write_to` 显式建 | 发布者 `request_band(name)` → master 广播 Band → 订阅者自动 join |
| **多消费者** | ✅ | ✅ | ❌（点对点） | ✅（多个策略可 join 同一 band_uid） |
| **典型场景** | Quote / Order / Trade / 系统元信息 | Asset/Position 快照 | OrderInput / 私有回报 | bar 生成器给"1min-bar"/"5min-bar"分别建 band |

---

### 四、Band 解决了什么问题

如果只有 PUBLIC/SYNC/Channel：
- **PUBLIC 太粗**：MD 把所有 Quote 都写到 PUBLIC，所有策略都能看到全部行情。如果一个 MD 类服务（比如 bar 生成器）想给不同订阅者发不同内容（1 分钟 bar、5 分钟 bar、自定义周期 bar），全塞 PUBLIC 会让所有策略都收到所有 bar，浪费。
- **Channel 太窄**：Channel 是点对点，bar 生成器要给 100 个策略发同一个 1min bar，得建 100 条 Channel，发布者写 100 次。

**Band 提供了“命名多播”**：发布者写一次到 `band_uid` 这条 journal，所有 enrolled 的策略各自 mmap 读同一份文件（上一轮对话里讲过的多读者机制），实现“写一次，多读者各自消费”。同时不同 band 之间数据隔离，策略可以选择性 join。

---

### 五、strategy101.h 里的 `subscribe_operator`

[strategy101.h:28](../apps/strategies/strategy101.h#L28)：

```cpp
// context->subscribe_operator("bar", "my-bar");
```

这行被注释掉了，而且全代码库**搜不到 `subscribe_operator` 的实现**——它只是预留的 strategy 层 API。从命名推测，设计意图是：
- 第一个参数 `"bar"` = MD 源 group
- 第二个参数 `"my-bar"` = band 名

即“显式订阅某个发布者的某个 band”。但当前实现里，Band 是**粗粒度自动 join** 的：策略只要 `subscribe("bar", ...)` 登记 MD 源，该源下所有 Band 都会被动接收。`subscribe_operator` 本来想做细粒度“只订阅 my-bar 这一个 band”，但这个 API 没落地。

---

### 六、当前代码里 Band 的实际使用情况

实际查下来，**Band 机制在当前代码库里用得很少**：
- `bar.cpp`（[bar.cpp:85](../src/wingchun/service/bar.cpp#L85)）生成 bar 后是 `get_writer(location::PUBLIC)->write(...)`，**没有用 band**，而是直接写 PUBLIC
- `request_band` 的实际调用方只有 `broker.h:94` 提供的转发包装 `[[maybe_unused]] uint32_t request_band(...)`——`[[maybe_unused]]` 这个标记也暗示它基本没人调

也就是说，Band 是框架预留下来的“命名多播”能力，基础设施完整（类型定义、master 路由、订阅者自动 join、`bands_` map 维护、`register_band`/`deregister_band`），但业务层基本没启用。如果未来要让 bar 生成器按周期分通道发布（1min/5min/15min 各自一条 journal，策略按需订阅），就可以用 `request_band("1min")` / `request_band("5min")` + `get_writer(band_uid)->write(bar)` 这一套来实现。

### 七、一句话总结

- **`Band`** = 命名订阅组，`{source_id=发布者 uid, dest_id=虚拟 band location uid}`，让 MD 类发布者在自己的 journal 里开多条独立命名子通道
- **`RequestWriteToBand`** = 发布者向 master 申请建一条 band，master 校验后广播 `Band` 事件，已 enrolled 该 MD 源的所有策略自动 `reader_->join(发布者, band_uid, time)` 开始读取
- 与 `Channel`（点对点，dest 是真实 app uid）、`PUBLIC`（全局广播）、`SYNC`（快照同步）并列，是第四种 dest_id 语义，专用于“MD 内的命名多播”场景

---

帮我罗列一下，按照 kungfu-cpp\config\supervisord.conf 这个配置启动服务，都会创建哪些 channel 和 journal

## 1. Category Enum Definition

**File:** `include/kungfu/longfist/enums.h:55-62`

There are exactly **4 categories** (no CACHED, no LEDGER):

```cpp
enum class category : int8_t { MD, TD, STRATEGY, SYSTEM };
```

---

## 2. Location Construction Mechanism

**File:** `include/kungfu/yijinjing/common.h:101-147`

The `location` struct constructor creates a unique name (`uname`) and UID:

```cpp
location(longfist::enums::mode m, longfist::enums::category c, std::string g, std::string n, locator_ptr l)
    : locator(std::move(l)), uname(fmt::format("{}/{}/{}/{}", longfist::enums::get_category_name(c), g, n,
                                               longfist::enums::get_mode_name(m))),
      uid(util::hash_str_32(uname)) {
  category = c;
  group = std::move(g);
  name = std::move(n);
  mode = m;
  location_uid = uid;
}
```

**Uname format:** `{category}/{group}/{name}/{mode}` (e.g. `system/master/master/live`)
**UID:** `hash_str_32(uname)` with seed=42

---

## 3. Binary Entry Points & Home Location Construction

### 3.1 kf_master — `apps/master.cpp`

**Entry point (main):** `apps/master.cpp:276-306`

```cpp
int main(int argc, char **argv) {
    // ...arg parsing...
    mode m = mode::LIVE;            // or DATA for "sim"
    auto loc = std::make_shared<locator>(m);
    auto home = location::make_shared(m, category::SYSTEM, "master", "master", loc);
    // ...
    master_app app(home, low_latency);
    app.run();
}
```

**Home location:**
- **Category:** `category::SYSTEM` ✓ (as you suspected)
- **Group:** `"master"`
- **Name:** `"master"`
- **Uname example:** `system/master/master/live` (LIVE mode) or `system/master/master/data` (sim/DATA mode)

---

### 3.2 kf_cached — `apps/cached.cpp` → `src/yijinjing/cache/cached.cpp`

**Entry point (main):** `apps/cached.cpp:17-53`

The app creates a `cached` service object. The home location is in the **constructor**:

**File:** `src/yijinjing/cache/cached.cpp:22-28`

```cpp
cached::cached(locator_ptr locator, mode m, bool low_latency)
    : apprentice(location::make_shared(m, category::SYSTEM, "service", "cached", std::move(locator)), low_latency),
      ...
```

**Home location:**
- **Category:** `category::SYSTEM` ✓ (NOT a separate CACHED category)
- **Group:** `"service"`
- **Name:** `"cached"`
- **Uname example:** `system/service/cached/live`

The `--group` and `--name` args in `main()` are **parsed but completely ignored** — the constructor hardcodes group="service" and name="cached".

---

### 3.3 kf_ledger — `apps/ledger.cpp` → `src/wingchun/service/ledger.cpp`

**Entry point (main):** `apps/ledger.cpp:17-53`

**File:** `src/wingchun/service/ledger.cpp:22-24`

```cpp
Ledger::Ledger(locator_ptr locator, mode m, bool low_latency)
    : apprentice(location::make_shared(m, category::SYSTEM, "service", "ledger", std::move(locator)), low_latency),
      ...
```

**Home location:**
- **Category:** `category::SYSTEM` ✓ (NOT a separate LEDGER category)
- **Group:** `"service"`
- **Name:** `"ledger"`
- **Uname example:** `system/service/ledger/live`

Again, `--group` / `--name` args are parsed but **ignored** — hardcoded values used.

---

### 3.4 kf_md — `apps/md.cpp` → `src/wingchun/broker/marketdata.cpp`

**Entry point (main):** `apps/md.cpp:30-73`

**File:** `src/wingchun/broker/marketdata.cpp:17-19`

```cpp
MarketDataVendor::MarketDataVendor(locator_ptr locator, const std::string &group, const std::string &name,
                                   bool low_latency)
    : BrokerVendor(location::make_shared(mode::LIVE, category::MD, group, name, std::move(locator)), low_latency) {}
```

**Home location:**
- **Category:** `category::MD` ✓ (correct as you suspected)
- **Group:** `group` argument (e.g. `"sim"`)
- **Name:** `name` argument (e.g. `"sim"`)
- **Uname example:** `md/sim/sim/live`
- **IMPORTANT:** Mode is **hardcoded to `mode::LIVE`** regardless of the `--mode` argument! The `m` variable from main() is never passed to the vendor constructor.

---

### 3.5 kf_td — `apps/td.cpp` → `src/wingchun/broker/trader.cpp`

**Entry point (main):** `apps/td.cpp:30-76`

**File:** `src/wingchun/broker/trader.cpp:21-22`

```cpp
TraderVendor::TraderVendor(locator_ptr locator, const std::string &group, const std::string &name, bool low_latency)
    : BrokerVendor(location::make_shared(mode::LIVE, category::TD, group, name, std::move(locator)), low_latency) {}
```

**Home location:**
- **Category:** `category::TD` ✓ (correct as you suspected)
- **Group:** `group` argument (e.g. `"sim"`)
- **Name:** `name` argument (e.g. `"sim"`)
- **Uname example:** `td/sim/sim/live`
- **IMPORTANT:** Same as MD — mode is **hardcoded to `mode::LIVE`**, ignoring `--mode`. The `--source` argument selects which TraderSim/implementation to use but does not affect the location.

---

### 3.6 kf_strategy — `apps/strategy.cpp` → `src/wingchun/strategy/runner.cpp`

**Entry point (main):** `apps/strategy.cpp:30-81`

**File:** `src/wingchun/strategy/runner.cpp:18-21`

```cpp
Runner::Runner(locator_ptr locator, const std::string &group, const std::string &name, mode m, bool low_latency,
               const std::string &arguments)
    : apprentice(location::make_shared(m, category::STRATEGY, group, name, std::move(locator)), low_latency),
      ...
```

**Home location:**
- **Category:** `category::STRATEGY` ✓ (correct as you suspected)
- **Group:** `group` argument (e.g. `"sim"`)
- **Name:** `name` argument (e.g. `"sim"`)
- **Mode:** `m` — properly passed through (LIVE/DATA/REPLAY/BACKTEST)
- **Uname example:** `strategy/sim/sim/live` or `strategy/sim/sim/data` for sim mode

---

## 4. hero.cpp: Well-Known Locations Construction

**File:** `src/yijinjing/practice/hero.cpp:30-45`

All hero-derived classes (every binary that inherits from `apprentice` → `hero`) initialize these 4 fixed locations using `make_system_location()`:

```cpp
hero::hero(io_device_ptr io_device)
    : begin_time_(time::now_in_nano()), end_time_(INT64_MAX),
      master_home_location_  (make_system_location("master", "master", io_device->get_locator())),
      master_cmd_location_   (make_system_location("master", encode(io_device), io_device->get_locator())),
      cached_home_location_  (make_system_location("service", "cached", io_device->get_locator())),
      ledger_home_location_  (make_system_location("service", "ledger", io_device->get_locator())),
      ...
```

Where `make_system_location()` is defined in `include/kungfu/yijinjing/practice/hero.h:23-27`:

```cpp
inline yijinjing::data::location_ptr make_system_location(const std::string &group, const std::string &name,
                                                          const data::locator_ptr &locator) {
  return yijinjing::data::location::make_shared(longfist::enums::mode::LIVE, longfist::enums::category::SYSTEM, group,
                                                name, locator);
}
```

### 4.1 master_home_location_
- **Category:** `SYSTEM`, **Mode:** `LIVE`
- **Group:** `"master"`, **Name:** `"master"`
- **Uname:** `system/master/master/live`
- Always LIVE mode regardless of actual runtime mode

### 4.2 cached_home_location_
- **Category:** `SYSTEM`, **Mode:** `LIVE`
- **Group:** `"service"`, **Name:** `"cached"`
- **Uname:** `system/service/cached/live`

### 4.3 ledger_home_location_
- **Category:** `SYSTEM`, **Mode:** `LIVE`
- **Group:** `"service"`, **Name:** `"ledger"`
- **Uname:** `system/service/ledger/live`

### 4.4 master_cmd_location_ (Command Channel Pseudo-Location)

**File:** `src/yijinjing/practice/hero.cpp:26-28` and `hero.cpp:33`

```cpp
inline std::string encode(const io_device_ptr &io_device) {
  return fmt::format("{:08x}", io_device->get_live_home()->uid);
}
// ...
master_cmd_location_(make_system_location("master", encode(io_device), io_device->get_locator())),
```

- **Category:** `SYSTEM`, **Mode:** `LIVE`
- **Group:** `"master"`
- **Name:** `"{:08x}"` formatted UID of this process's own **live_home** UID (8 hex chars, lowercase)
- **Uname example:** `system/master/a1b2c3d4/live` (where `a1b2c3d4` is this binary's own live_home uid in hex)

This creates a **per-process unique command channel** name so master can address commands to a specific binary individually.

---

## 5. hash_str_32 Implementation

### Declaration: `include/kungfu/yijinjing/util/util.h:10-24`

```cpp
#define KUNGFU_HASH_SEED 42

/**
 * Murmur Hash 2  (NOTE: comment says "2" but actually uses MurmurHash3!)
 * @param key content to be hashed
 * @param len length of key
 * @param seed
 * @return hash result
 */
uint32_t hash_32(const unsigned char *key, int32_t length, uint32_t seed = KUNGFU_HASH_SEED);
uint32_t hash_str_32(const std::string &key, uint32_t seed = KUNGFU_HASH_SEED);
```

### Implementation: `src/yijinjing/util/hash.cpp:17-25`

```cpp
uint32_t hash_32(const unsigned char *key, int32_t length, uint32_t seed) {
  uint32_t h;
  MurmurHash3_x86_32(key, length, seed, &h);
  return h;
}

uint32_t hash_str_32(const std::string &key, uint32_t seed) {
  return hash_32(reinterpret_cast<const unsigned char *>(key.c_str()), key.length(), seed);
}
```

**Algorithm: MurmurHash3 x86 32-bit (NOT DJB2, NOT FNV, NOT MurmurHash2)**
- Default seed: `42` (`KUNGFU_HASH_SEED`)
- Uses `MurmurHash3_x86_32()` from the included `MurmurHash3.h`
- Comment header incorrectly says "Murmur Hash 2" but the actual call is `MurmurHash3_x86_32`

**To compute a UID conceptually:**
```
uname = "{category_name}/{group}/{name}/{mode_name}"
uid = MurmurHash3_x86_32(uname_bytes, uname_length, seed=42)
```

Example (LIVE mode):
- master uid = MurmurHash3_x86_32("system/master/master/live", 25, 42)
- cached uid = MurmurHash3_x86_32("system/service/cached/live", 25, 42)
- MD sim = MurmurHash3_x86_32("md/sim/sim/live", 14, 42)

---

## Summary Table

| Binary | Entry File | Category | Group | Name | Mode | Notes |
|--------|-----------|----------|-------|------|------|-------|
| **kf_master** | `apps/master.cpp` | **SYSTEM** | `"master"` | `"master"` | from --mode (LIVE/DATA) | correct |
| **kf_cached** | `apps/cached.cpp` → `src/yijinjing/cache/cached.cpp` | **SYSTEM** | `"service"` | `"cached"` | from --mode | --group/--name args IGNORED |
| **kf_ledger** | `apps/ledger.cpp` → `src/wingchun/service/ledger.cpp` | **SYSTEM** | `"service"` | `"ledger"` | from --mode | --group/--name args IGNORED |
| **kf_md** | `apps/md.cpp` → `src/wingchun/broker/marketdata.cpp` | **MD** | user `--group` | user `--name` | **hardcoded LIVE** | --mode IGNORED |
| **kf_td** | `apps/td.cpp` → `src/wingchun/broker/trader.cpp` | **TD** | user `--group` | user `--name` | **hardcoded LIVE** | --mode IGNORED |
| **kf_strategy** | `apps/strategy.cpp` → `src/wingchun/strategy/runner.cpp` | **STRATEGY** | user `--group` | user `--name` | from --mode | correct |

**Important:** There are NO separate `CACHED` or `LEDGER` categories — both cached and ledger use `category::SYSTEM`.

Now I need to understand what happens during the `strategy` service startup - specifically what calls does `RuntimeContext::ensure_connect` make for the `sim` TD and `sim` MD sources. Let me check the relevant runtime code for the full channel establishment flow, and look at how strategy registers its context with md/td.

Now let me look at what cached and ledger do - specifically, what channels do they connect to.

I'll examine both files to understand the channel/journal setup for cached and ledger services. Let me read them in parallel.

Now let me examine the base class `apprentice` to understand inherited channel setup, and also the `broker_client_` and `bookkeeper_` classes' `on_start()` methods, since ledger delegates to them.

Let me find the relevant source files directly using Glob and Grep.

Good, now let me read the key files to understand the full channel setup. Let me read apprentice.cpp (base class), and also find broker_client and bookkeeper implementations.

Now let me quickly verify the header files for class declarations.

Now I have all the information needed. Here is a comprehensive report.

---

## Channel & Journal Establishment Analysis: Cached Service vs Ledger Service

---

### 1. CACHED SERVICE (`src/yijinjing/cache/cached.cpp`)

**Base class:** `yijinjing::practice::apprentice` (cached.h:18)

#### 1A. Inherited from `apprentice::react()` (apprentice.cpp:147-223)

Before `cached::on_react()` even runs, the base class `apprentice::react()` does:

| Location | Action |
|---|---|
| apprentice.cpp:192 (LIVE mode, self-register event) | `reader_->join(master_cmd_location_, get_live_home_uid(), begin_time_)` — **reads from master_cmd → cached** (private command channel) |
| apprentice.cpp:206 (cached_register_event) | Calls `request_cached_reader_writer()`: since `get_live_home_uid() == cached_home_location_->uid`, it does NOT call `request_write_to/request_read_from`; instead it self-sends `RequestCachedDone` to master_cmd. (apprentice.cpp:99-104) |
| apprentice.cpp:296 (`expect_start()`) | `reader_->join(master_home_location_, location::PUBLIC, begin_time_)` — **reads from master home PUBLIC channel** (to receive `RequestStart`) |

#### 1B. `cached::on_react()` (cached.cpp:30-62)

```cpp
// cached.cpp:30-62
void cached::on_react() {
  events_ | is(Location::tag) | $$(on_location(event));                               // [1]
  events_ | is(Register::tag) | $$(register_triggger_clear_cache_shift(...));         // [2]
  events_ | is(Register::tag) | $$(register_trigger_listen_public(...));              // [3] KEY!
  events_ | is(RequestCached::tag) | $([&](const event_ptr &event) { ... });          // [4]
}
```

**Key handler [3] — `register_trigger_listen_public`** (cached.cpp:183-195):

```cpp
void cached::register_trigger_listen_public(int64_t gen_time, const Register &register_data) {
  auto app_uid = register_data.location_uid;
  auto app_location = get_location(app_uid);

  if (app_location->category != category::TD) {  // ONLY TD!
    return;
  }

  // only public no sync
  reader_->join(app_location, location::PUBLIC, gen_time);      // <-- reads TD PUBLIC
  make_cache_shift(app_uid, location::PUBLIC);
}
```

**→ Cached only joins PUBLIC channels of TD apps. It does NOT join PUBLIC/SYNC for MD or STRATEGY here.**

#### 1C. `cached::on_start()` (cached.cpp:64-71)

```cpp
void cached::on_start() {
  events_ | is(Channel::tag) | $$(inspect_channel(event->gen_time(), event->data<Channel>())); // [A]
  events_ | is(CacheReset::tag) | $$(on_cache_reset(event));                                   // [B]
  events_ | instanceof <journal::frame>() | filter([&](const event_ptr &event) {
                         auto source_id = event->source();
                         return source_id != master_home_location_->uid
                            and source_id != master_cmd_location_->uid;
                       }) | $$(feed(event));                                                    // [C]
}
```

**Key handler [A] — `inspect_channel`** (cached.cpp:160-165):

```cpp
void cached::inspect_channel(int64_t trigger_time, const Channel &channel) {
  if (channel.source_id != get_live_home_uid() and channel.dest_id != get_live_home_uid()) {
    reader_->join(get_location(channel.source_id), channel.dest_id, trigger_time);
    make_cache_shift(channel.source_id, channel.dest_id);
  }
}
```

**→ This is a reactive "observe all" mechanism. For ANY Channel event (announced by master) that doesn't involve cached itself, cached joins the (source → dest) journal pair. This effectively catches all TD→strategy, strategy→TD, etc. channels dynamically.**

#### 1D. Does cached connect to `master_cmd_location`?

**Yes.** Two places:
- `apprentice::react()` at apprentice.cpp:192: `reader_->join(master_cmd_location_, get_live_home_uid(), ...)` — reads from master_cmd's writer to cached.
- `cached::mark_request_cached_done()` at cached.cpp:87 writes TO master_cmd via `get_writer(master_cmd_location_->uid)`.

#### 1E. Does cached read/write PUBLIC/SYNC of ALL apps?

- **PUBLIC:** Reads PUBLIC of **TD only** explicitly via `register_trigger_listen_public` (cached.cpp:187,192).
- **SYNC:** Cached does NOT explicitly join any SYNC channels in its own handlers.
- **All other channels:** Joined dynamically via `inspect_channel` on every `Channel::tag` event (cached.cpp:160-165), so any source→dest channel the master registers gets snooped by cached.
- **MD/STRATEGY PUBLIC:** Not explicitly listened to by the Register handler. But if a Channel event for a strategy→X or MD→X channel fires, `inspect_channel` will join that (source, dest_id) pair. However, that's different from joining (MD, PUBLIC).

---

### 2. LEDGER SERVICE (`src/wingchun/service/ledger.cpp`)

**Base class:** `yijinjing::practice::apprentice` (ledger.h:20)
**Contains:** `broker::AutoClient broker_client_` (ledger.h:39) + `book::Bookkeeper bookkeeper_` (ledger.h:40)

#### 2A. Inherited from `apprentice::react()` — SAME as cached.

Additionally, the apprentice base class checks if cached registers and then calls `request_cached_reader_writer()` (apprentice.cpp:206). **Ledger is NOT cached, so it actually runs:**
```cpp
// apprentice.cpp:96-97
request_write_to(now(), cached_home_location_->uid);   // ledger writes TO cached
request_read_from(now(), cached_home_location_->uid, now()); // ledger reads FROM cached
```

#### 2B. Ledger does NOT override `on_react()`. So only base `apprentice::react()` runs.

#### 2C. `Ledger::on_start()` (ledger.cpp:32-60)

```cpp
void Ledger::on_start() {
  broker_client_.on_start(events_);            // [1] KEY: delegates to AutoClient
  bookkeeper_.on_start(events_);               // [2] delegates to Bookkeeper
  bookkeeper_.guard_positions();
  // ... event handlers for data, nothing channel-related ...
  events_ | is(Channel::tag) | $$(inspect_channel(event->gen_time(), event->data<Channel>())); // [3]
  // ... timers for asset_sync / position_sync ...
  refresh_books();                             // [4]
}
```

**[1] `broker_client_.on_start(events_)` → `Client::on_start` (broker/client.cpp:145-150):**

```cpp
void Client::on_start(const rx::connectable_observable<event_ptr> &events) {
  events | is(Register::tag) | $$(connect(event, event->data<Register>()));
  events | is(Band::tag) | $$(connect(event, event->data<Band>()));
  events | is(BrokerStateUpdate::tag) | $$(update_broker_state(...));
  events | is(Deregister::tag) | $$(update_broker_state(...));
}
```

And since ledger uses `AutoClient` (all `should_connect_*` return true), the `connect(Register)` at broker/client.cpp:152-173 does:

```cpp
// For MD:
app_.request_write_to(app_.now(), app_uid);                          // write to MD
app_.request_read_from_public(app_.now(), app_uid, resume_time_point); // read MD PUBLIC

// For TD:
app_.request_write_to(app_.now(), app_uid);                           // write to TD
app_.request_read_from(app_.now(), app_uid, resume_time_point);       // read from TD (private)
app_.request_read_from_public(app_.now(), app_uid, resume_time_point); // read TD PUBLIC
app_.request_read_from_sync(app_.now(), app_uid, resume_time_point);  // read TD SYNC <-- KEY!

// For STRATEGY:
app_.request_write_to(app_.now(), app_location->uid);                 // write to strategy
app_.request_read_from(app_.now(), app_location->uid, resume_time_point);   // read strategy private
app_.request_read_from_public(app_.now(), app_location->uid, resume_time_point); // read strategy PUBLIC
```

**→ Ledger (via AutoClient) reads from ALL THREE categories: strategy private/public, TD private/public/sync, MD public.**

**[3] `inspect_channel` (ledger.cpp:154-167):**

```cpp
void Ledger::inspect_channel(int64_t trigger_time, const Channel &channel) {
  auto source_location = get_location(channel.source_id);
  auto is_from_account = source_location->category == category::TD;

  if (channel.source_id == cached_home_location_->uid or channel.dest_id == cached_home_location_->uid) {
    return;  // skip cached channels
  }
  if (channel.source_id != get_live_home_uid() and channel.dest_id != get_live_home_uid()) {
    reader_->join(source_location, channel.dest_id, trigger_time);  // <-- snoop on others' channels
  }
  if (channel.dest_id == get_live_home_uid() and has_writer(channel.source_id) and is_from_account) {
    write_book_reset(trigger_time, channel.source_id);   // reset book when account channel connects to us
  }
}
```

**[4] `refresh_books()` (ledger.cpp:72-79):**

```cpp
void Ledger::refresh_books() {
  for (const auto &pair : bookkeeper_.get_books()) {
    if (pair.second->asset.ledger_category == LedgerCategory::Account) {
      refresh_account_book(now(), pair.first);
    }
    request_write_to(now(), pair.first);  // <-- writes to every book (account/strategy)
  }
}
```

#### 2D. `Bookkeeper::on_start()` (bookkeeper.cpp:63-84)

**Does NOT do any channel join or request_read/write calls.** It only subscribes to data tags already flowing through the existing channels (Quote, Order, Trade, Asset/AssetMargin/Position from SYNC channel via `fork<...>(location::SYNC, ...)` at lines 73-76).

---

### 3. SUMMARY OF ALL CHANNELS ESTABLISHED

| # | Who | What | Where |
|---|---|---|---|
| **Base apprentice (both cached & ledger):** | | | |
| 1 | both | `reader_->join(master_home, PUBLIC)` | apprentice.cpp:296 |
| 2 | both (LIVE) | `reader_->join(master_cmd, self_home)` | apprentice.cpp:192 |
| 3 | ledger (not cached) | `request_write_to(cached)` + `request_read_from(cached)` | apprentice.cpp:96-97 |
| **Cached-specific:** | | | |
| 4 | cached | On `Channel::tag`: `reader_->join(source, dest_id)` for any non-self channel | cached.cpp:160-165, 65 |
| 5 | cached | On `Register::tag` of **TD** only: `reader_->join(td_location, PUBLIC)` | cached.cpp:183-195, 33 |
| 6 | cached | Writes to `master_cmd_location` (RequestCachedDone) | cached.cpp:86-91 |
| 7 | cached | **No explicit SYNC joins. No MD PUBLIC. No STRATEGY PUBLIC.** | |
| **Ledger-specific (via AutoClient::connect on Register):** | | | |
| 8 | ledger | For every MD: `request_write_to(MD)` + `request_read_from_public(MD)` | broker/client.cpp:156-159 |
| 9 | ledger | For every TD: `request_write_to(TD)` + `request_read_from(TD)` + `request_read_from_public(TD)` + **`request_read_from_sync(TD)`** | broker/client.cpp:161-167 |
| 10 | ledger | For every STRATEGY: `request_write_to(strategy)` + `request_read_from(strategy)` + `request_read_from_public(strategy)` | broker/client.cpp:168-173 |
| 11 | ledger | On `Channel::tag`: `reader_->join(source, dest_id)` (skipping cached channels) | ledger.cpp:154-163, 42 |
| 12 | ledger | `refresh_books()`: `request_write_to()` every known account/strategy book | ledger.cpp:72-79, 77 |

---

### Direct Answers To Your Questions:

**Q1 (cached): Does it connect to master_cmd_location?**
- **Yes.** It reads from master_cmd→self (apprentice.cpp:192) and writes RequestCachedDone to master_cmd (cached.cpp:87).

**Q1 (cached): Does it read/write PUBLIC/SYNC of all apps?**
- It explicitly reads **PUBLIC for TD apps** via Register handler (cached.cpp:192).
- It does **NOT** read SYNC for TD, nor PUBLIC/SYNC for MD or STRATEGY via Register handler.
- It **dynamically joins arbitrary (source, dest_id) channels** via `inspect_channel` on Channel events (cached.cpp:162).

**Q2 (ledger): Does ledger read from strategy? from TD? from MD?**
- **STRATEGY: YES** — private + PUBLIC channels (broker/client.cpp:169-171).
- **TD: YES** — private + PUBLIC + **SYNC** channels (broker/client.cpp:163-165).
- **MD: YES** — PUBLIC channel only (broker/client.cpp:157-158).

**Q3: Do cached or ledger register event handlers in react() that call request_read_from_public/sync on all TD/MD/STRATEGY locations?**
- **cached on_react():** Only `register_trigger_listen_public` for **TD→PUBLIC** (cached.cpp:183-195). Uses `reader_->join()` directly (not request_read_from_public, since cached itself already has authority as a system app).
- **ledger on_react():** Does NOT override on_react() (only base apprentice). But in **on_start()**, ledger delegates to `broker_client_.on_start()`, which registers a `Register::tag` → `connect()` handler that DOES call `request_read_from_public` / `request_read_from_sync` / `request_read_from` / `request_write_to` for all MD/TD/STRATEGY locations dynamically as they register.

按 [supervisord.conf](../config/supervisord.conf) 的启动顺序，完整分析如下。所有 `uid` 的计算规则：`MurmurHash3_x86_32(uname_bytes, length, seed=42)`，其中 uname 格式 `{category}/{group}/{name}/{mode}`（[common.h:109-118](../include/kungfu/yijinjing/common.h#L109-L118)、[hash.cpp:17-25](../src/yijinjing/util/hash.cpp#L17-L25)）。journal 文件格式：`{dest_id:08x}.{page_id}.journal`，存放路径由 locator 决定（见 [page.cpp:67-70](../src/yijinjing/journal/page.cpp#L67-L70)）。

---

## 一、6 个进程的 Location 身份表

| # | 进程 | 二进制 | category | group | name | mode† | 示例 uname |
|---|------|--------|----------|-------|------|-------|-------------|
| M | master | `kf_master` | SYSTEM | master | master | LIVE / DATA‡ | `system/master/master/live` |
| C | cached | `kf_cached` | SYSTEM | service | cached | LIVE / DATA‡ | `system/service/cached/live` |
| L | ledger | `kf_ledger` | SYSTEM | service | ledger | LIVE / DATA‡ | `system/service/ledger/live` |
| D | md_sim | `kf_md` | **MD** | sim | sim | **LIVE**§ | `md/sim/sim/live` |
| T | td_sim | `kf_td` | **TD** | sim | sim | **LIVE**§ | `td/sim/sim/live` |
| S | strategy | `kf_strategy` | **STRATEGY** | sim | sim | LIVE / DATA‡ | `strategy/sim/sim/live` |

> † `supervisord.conf` 启动时未传 `--mode`，所有进程默认 `mode=LIVE`（master 见 [master.cpp:276-303](../apps/master.cpp#L276-L303)、strategy 见 [strategy.cpp:30-67](../apps/strategy.cpp#L30-L67)）。
> ‡ master/cached/ledger/strategy 的 home mode 由 `--mode` 正确传入。
> § md/td 构造 home 时 hardcode `mode::LIVE`，忽略 `--mode`（[marketdata.cpp:17-19](../src/wingchun/broker/marketdata.cpp#L17-L19)、[trader.cpp:21-22](../src/wingchun/broker/trader.cpp#L21-L22)）。
> 同时，每个进程还内置 **4 个系统级 known location**：`master_home`、`master_cmd_{:08x}`（自己 uid 的 8 位 hex）、`cached_home`、`ledger_home`，全部 `SYSTEM` 类 + `mode::LIVE`，见 [hero.cpp:30-45](../src/yijinjing/practice/hero.cpp#L30-L45)。

---

## 二、Master 启动（先于所有 app）

Master 构造时直接开一条 writer（[master.cpp:33-36](../src/yijinjing/practice/master.cpp#L33-L36)）：

### Master 自己持有的 writer

| dest_id | 含义 | 代码位置 |
|---------|------|----------|
| **PUBLIC = 0** | master 对整个系统广播（Location / Register / Channel / Band / Deregister / TradingDay 等） | [master.cpp:35](../src/yijinjing/practice/master.cpp#L35) |

### Master 自己 join 的 reader（启动初期）

| source_location | dest_id | 含义 |
|-----------------|---------|------|
| `master_home` 自身 | PUBLIC | 读自己写的 PUBLIC（一般没用上，但会 join），基类 apprentice 自动完成 |

> 说明：master_cmd / cached / ledger 这 4 个系统 location 在 master 启动时会注册并建立通道，跟下面“注册流程”一致。

---

## 三、每个 App 注册时 Master 给它开的默认通道（`register_app`）

每个 app 注册都会触发 [master.cpp:90-142](../src/yijinjing/practice/master.cpp#L90-L142) 的 `register_app`，这段代码**为每个 app 统一建立 3 条 dest 的 journal + 3 对 Channel**。以“App X（home uid = `Ux`）”为例，其 home location = `Lx`，注册瞬间生成的 `master_cmd_伪_location` uname = `system/master/{Ux:08x}/live`，记为 `CMx`，其 uid = `Ucx`。

### 3A. Master 给 App X 分配的 3 条 writer（App X 作为 source）

| writer 的 dest_id | 对应 journal 文件 (在 Lx 下) | page 大小（MD/TD/STRATEGY/SYSTEM） | 用途 |
|---|---|---|---|
| **PUBLIC = 0** | `Lx/00000000.1.journal`、`00000000.2.journal`... | MD=128MB / TD=1MB / STRATEGY=1MB / SYSTEM=1MB ‖ [page.h:84-94](../include/kungfu/yijinjing/journal/page.h#L84-L94) | 广播给所有消费者的公共数据（Quote / BrokerStateUpdate / Channel 广播 / 订单状态 等） |
| **SYNC = 1** | `Lx/00000001.1.journal`、`00000001.2.journal`... | MD=1MB / TD=16MB / STRATEGY=16MB / SYSTEM=1MB | 只对 TD / STRATEGY 有实际数据：Asset / Position / AssetMargin / PositionEnd 等账册全量快照 |
| **Ucx** (master_cmd 伪 location uid) | `Lx/{Ucx:08x}.1.journal`... | 1MB | master 给 App X 的**一对一命令通道**：Register 回执 / SessionStart / TimeReset / TradingDay / RequestStart / RequestCachedDone / 注册信息回灌 / channels / bands / locations |

> 注册流程里由 `require_write_to(..., Lx.uid, {PUBLIC|SYNC|Ucx})` 发出（[master.cpp:130-132](../src/yijinjing/practice/master.cpp#L130-L132)），App X 收到 `RequestWriteTo` 事件后在自己的 `writers_` map 里 emplace 对应 writer（[apprentice.cpp:255-259](../src/yijinjing/practice/apprentice.cpp#L255-L259)）。

### 3B. Master 作为 reader join 这 3 条

| source | dest_id | 代码位置 |
|--------|---------|----------|
| `Lx` (App X home) | PUBLIC | [master.cpp:120](../src/yijinjing/practice/master.cpp#L120) |
| `Lx` | SYNC | [master.cpp:121](../src/yijinjing/practice/master.cpp#L121) |
| `Lx` | `Ucx` (master_cmd) | [master.cpp:122](../src/yijinjing/practice/master.cpp#L122) |

Master 把 (Lx, 0) / (Lx, 1) / (Lx, Ucx) 三条 journal 全部 join，作为总控，监听所有 app 的所有流量。

### 3C. Master 同时给 App X 开一条反向 writer（master_cmd → App X）

| writer 的 source | dest_id | 文件路径 | 用途 |
|---|---|---|---|
| `CMx` (master_cmd location) | **Ux**（App X 自己的 uid） | `CMx/{Ux:08x}.{page}.journal` | App X 通过 master_cmd 发回给 master 的“请求回执”通道（register 应答等）。由 `open_writer_at(CMx, Ux)` 打开，在 [master.cpp:111](../src/yijinjing/practice/master.cpp#L111)。 |

App X 在基类 apprentice::react 的 Register 回调中会 reader_->join(`CMx`, Ux)（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)），接收 master 下达到自己的命令。

### 3D. Master 广播到 PUBLIC 的 2 条事件（所有 app 都会收到）

1. `Location(Lx)`：Lx 的身份广播（[master.cpp:127](../src/yijinjing/practice/master.cpp#L127)）
2. `Register(Lx info)`：注册信息广播（[master.cpp:128](../src/yijinjing/practice/master.cpp#L128)）

---

## 四、按注册顺序的完整 Channel 清单（Channel = source→dest）

Channel 注册时 master 都会写一条 `Channel{source_id,dest_id}` 到 **master PUBLIC**（参见 [master.cpp:292-296](../src/yijinjing/practice/master.cpp#L292-L296) / [310-314](../src/yijinjing/practice/master.cpp#L310-L314)）。下表把启动流程里所有必然触发的 Channel 列出来，按启动顺序（master → cached → ledger → md → td → strategy）展开。

符号说明：U_M = master uid, U_C = cached uid, U_L = ledger uid, U_D = md(sim/sim) uid, U_T = td(sim/sim) uid, U_S = strategy(sim/sim) uid。
CMX = master_cmd_伪X 的 uid（`system/master/{UX:08x}/live` 的 hash）。

### 阶段 1：cached 注册（第 2 个进程）

**默认 3 条 (Lx,dest) writer 通道（所有 app 都有，不再重复列）：** (C,PUBLIC), (C,SYNC), (C,CMC)

| Channel (source → dest) | 由谁触发 | 代码路径 | 备注 |
|---|---|---|---|
| **M → CMC**（master_cmd_cached 的 PUBLIC，作为 master 命令入口） | 注册时 `get_io_device()->open_writer_at(CMC, U_C)` 返回的 writer 不需要 Channel 事件 | [master.cpp:111](../src/yijinjing/practice/master.cpp#L111) | |
| **M → C** (master home → cached private) | apprentice base class 在 `expect_start` 之前会从 master 读取 **RequestStart**（不需要 Channel，直接在 reader join master PUBLIC） | [apprentice.cpp:296](../src/yijinjing/practice/apprentice.cpp#L296) | 非 Channel，是基类直接 `reader_->join(master_home_location_, PUBLIC)` |
| **cached 自己读 (CM C → U_C)**（master 下达给 cached 进程的私命令） | `reader_->join(CM C, U_C, begin_time)` | [apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192) | 非 Channel 显式广播，但是基类的必建关系 |

### 阶段 2：ledger 注册（第 3 个进程）

默认通道同上。

| Channel (source → dest) | 由谁触发 | 代码路径 | 备注 |
|---|---|---|---|
| **L → C** (ledger → cached private) | apprentice 基类非 cached 进程会在 `request_cached_reader_writer` 里 `request_write_to(now(), U_C)` | [apprentice.cpp:96](../src/yijinjing/practice/apprentice.cpp#L96) | ledger 往 cached 写请求；master 通过 `RequestWriteTo` → `on_request_write_to` → `register_channel` 广播 `Channel{U_L, U_C}` |
| **C → L** (cached → ledger private) | 同上 `request_read_from(now(), U_C, now())` | [apprentice.cpp:97](../src/yijinjing/practice/apprentice.cpp#L97) | ledger 读 cached；master 广播 `Channel{U_C, U_L}` |
| **ledger 读 (CM_L → U_L)**（同上 master_cmd） | [apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192) | | |

**Ledger 作为 BrokerService + AutoClient 会动态对后续所有 Register（md/td/strategy）发起全部 4 类连接（见下）。**

### 阶段 3：md_sim 注册（第 4 个进程，category=MD）

默认 3 条 (D,PUBLIC), (D,SYNC), (D,CMD)。策略侧的 AutoClient（ledger / strategy）在收到 `Register{MD, sim/sim}` 时触发 [client.cpp:156-159](../src/wingchun/broker/client.cpp#L156-L159)：

```cpp
if (MD and should_connect_md) {
  app_.request_write_to(now(), U_D);           // 申请写 MD 的私有通道
  app_.request_read_from_public(now(), U_D);    // 申请读 MD PUBLIC
}
```

| Channel (source → dest) | master side handler | 广播出来的 Channel？ | 备注 |
|---|---|---|---|
| **L → D** (ledger → md private) | `request_write_to` → [master.cpp:279-297](../src/yijinjing/practice/master.cpp#L279-L297) | ✅ `Channel{U_L, U_D}` | ledger 向 MD 写 InstrumentKey（订阅） |
| **S → D** (strategy → md private) | 同上（strategy 侧 connect） | ✅ `Channel{U_S, U_D}` | strategy 在 `RuntimeContext::subscribe` 里 `broker_client_.connect` → `request_write_to`（strategy 的 PassiveClient 只在 `enrolled_md_locations_` 有 MD 时连；由 strategy101 `subscribe("sim",...)` 触发，见 [runtime.cpp:69-78](../src/wingchun/strategy/runtime.cpp#L69-L78)、[client.cpp:156-159](../src/wingchun/broker/client.cpp#L156-L159)） |
| **D → L (PUBLIC=0)**（MD PUBLIC → ledger 可读） | `request_read_from_public(ledger→D)` → [master.cpp:317-320](../src/yijinjing/practice/master.cpp#L317-L320)，内部 `require_write_to(U_D, PUBLIC)` | ✅ 但它复用的是 3A 阶段已创建的 (D,0) writer，不会再发 Channel；master 只发 RequestReadFromPublic 让 ledger 自己 `do_read_from(..., dest_id=0)`（[apprentice.cpp:251](../src/yijinjing/practice/apprentice.cpp#L251)）→ `reader_->join(LD, 0)` | 这类“PUBLIC/SYNC 读请求”不会产生新 Channel；因为所有 app 在 register_app 时已默认能写 PUBLIC / SYNC（[master.cpp:130-131](../src/yijinjing/practice/master.cpp#L130-L131)），PUBLIC/SYNC 是所有人共享的 dest |
| **D → S (PUBLIC=0)**（MD PUBLIC → strategy 可读） | 同上，由 strategy PassiveClient 在 MD Ready 后 `request_read_from_public` | 同上，不产生 Channel | strategy101 触发 |

**Cached 侧：** 对 MD 的 Register 事件，cached 在 `register_trigger_listen_public` 里 **只对 category==TD 分支 join PUBLIC**（[cached.cpp:187](../src/yijinjing/cache/cached.cpp#L187)），**MD 的 PUBLIC 不主动 join。** 但 cached 有 `inspect_channel`，会对所有 Channel 事件中非自己相关的 `(source,dest_id)` pair join，所以只要 ledger/strategy 跟 MD 建立了私有通道，Channel 事件会触发 cached 把 (MD, dest_id=U_L) 和 (MD, dest_id=U_S) 两条私有 journal 也 join 进来做缓存（[cached.cpp:160-165](../src/yijinjing/cache/cached.cpp#L160-L165)）。

### 阶段 4：td_sim 注册（第 5 个进程，category=TD）

默认 3 条 (T,PUBLIC), (T,SYNC), (T,CMT)。AutoClient / PassiveClient 对 TD 连 4 条（[client.cpp:161-167](../src/wingchun/broker/client.cpp#L161-L167)）：

```cpp
if (TD and should_connect_td) {
  app_.request_write_to(now(), U_T);                // 写 TD 私有
  app_.request_read_from(now(), U_T);               // 读 TD 给自己的私有
  app_.request_read_from_public(now(), U_T);        // 读 TD PUBLIC
  app_.request_read_from_sync(now(), U_T);          // 读 TD SYNC
}
```

| Channel (source → dest) | 产生新 Channel? | 备注 |
|---|---|---|
| **L → T** (ledger → td 私有) | ✅ `Channel{U_L, U_T}` | ledger 给 TD 写 ResetBookRequest 等；Ledger::on_start 的 `refresh_books()` 还会再调用一次 `request_write_to(U_T)`（[ledger.cpp:72-79](../src/wingchun/service/ledger.cpp#L72-L79)），幂等 |
| **S → T** (strategy → td 私有) | ✅ `Channel{U_S, U_T}` | strategy 下 OrderInput 的通道；由 strategy `ensure_connect` 中 `broker_client_.connect(TD Register)` 触发的 `request_write_to` 建立（[runtime.cpp:355-370](../src/wingchun/strategy/runtime.cpp#L355-L370) + [client.cpp:152-167](../src/wingchun/broker/client.cpp#L152-L167)） |
| **T → L (dest=U_L)**（TD → ledger 的私有订单回报） | ✅ `Channel{U_T, U_L}` | `request_read_from(ledger→T)` → master `on_request_read_from`（[master.cpp:299-315](../src/yijinjing/practice/master.cpp#L299-L315)）创建 |
| **T → S (dest=U_S)**（TD → strategy 的私有订单回报） | ✅ `Channel{U_T, U_S}` | 同上，由 strategy 的 `request_read_from` 创建 |
| **T → L (PUBLIC=0)**（TD PUBLIC → ledger 可读 BrokerStateUpdate） | ❌ 不产生 Channel，复用已注册的 (T,0) writer；ledger `reader_->join(LT, 0)` | |
| **T → S (PUBLIC=0)**（TD PUBLIC → strategy 可读 BrokerStateUpdate） | ❌ 同上复用，strategy `reader_->join(LT, 0)` | |
| **T → L (SYNC=1)**（TD SYNC → ledger 读 Asset/Position 快照） | ❌ 不产生 Channel，复用 (T,1) writer；ledger `reader_->join(LT, 1)` | 由 `request_read_from_sync` → `do_read_from(..., location::SYNC)`（[apprentice.cpp:253](../src/yijinjing/practice/apprentice.cpp#L253)） |
| **T → S (SYNC=1)**（TD SYNC → strategy 读快照） | ❌ 同上复用 | strategy 的 `request_read_from_sync(TD)` 也会发起；注意 strategy 的 PassiveClient 只在 `ensure_connect` 对 TD 发（[client.cpp:161-167](../src/wingchun/broker/client.cpp#L161-L167)） |

**Cached 对 TD Register 的专门处理：** cached 对 TD 的 Register 事件直接 `reader_->join(LT, PUBLIC)`（[cached.cpp:192](../src/yijinjing/cache/cached.cpp#L192)）缓存 TD PUBLIC 所有帧。对上面产生的 Channel（L→T、S→T、T→L、T→S）cached 也会通过 `inspect_channel` 全部 join，因为 `channel.source_id != cached_uid && channel.dest_id != cached_uid` 恒为 true。

### 阶段 5：strategy 注册（第 6 个进程）

默认 3 条 (S,PUBLIC), (S,SYNC), (S,CMS)。Ledger 的 AutoClient 对 category=STRATEGY 连 3 条（[client.cpp:168-173](../src/wingchun/broker/client.cpp#L168-L173)）：

```cpp
if (STRATEGY and should_connect_strategy) {
  app_.request_write_to(now(), U_S);
  app_.request_read_from(now(), U_S);
  app_.request_read_from_public(now(), U_S);
}
```

| Channel (source → dest) | 产生新 Channel? | 备注 |
|---|---|---|
| **L → S** (ledger → strategy 私有) | ✅ `Channel{U_L, U_S}` | |
| **S → L (dest=U_L)**（strategy → ledger 的订单/对账） | ✅ `Channel{U_S, U_L}` | 由 ledger 的 `request_read_from` 创建 |
| **S → L (PUBLIC=0)** | ❌ 复用 (S,0)，ledger join strategy PUBLIC | |

**Strategy 对已注册的 D / T 的 PassiveClient 连接：** 在 Runner::prepare / RuntimeContext::ensure_connect 阶段会检查 `app_.get_registry()` 和 `app_.get_bands()`，对其中的 MD/TD 调用 connect。由于 D/T 先注册，这一步会立即执行上面阶段 3/4 的所有 request_write/request_read 动作（即 D↔S 和 T↔S 的所有 private channel 建立）。

---

## 五、最终 Journal 文件清单（按 source location 分组）

按 `find_page_size` 规则（[page.h:84-94](../include/kungfu/yijinjing/journal/page.h#L84-L94)），在 locator 的 JOURNAL 目录下会有这些 location 子目录，各含若干 journal page 文件。

### 5A. SYSTEM/master/{mode}（master home location）
| dest_id | 文件名（首 page） | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB | master（全局广播 Location/Register/Channel/Band/Deregister/TradingDay 等） | 所有进程（基类 `reader_->join(master_home, PUBLIC)`） |
| 1 (SYNC) | `00000001.1.journal` | 1MB | master（register_app 默认开，实际空） | master（默认 join，实际没数据） |
| `U_C` | `{U_C:08x}.1.journal` | 1MB | master（给 cached 的私命令？——实际 master 通过 CM C 写，这里是 master 对 cached 的备用通道，默认开但没数据） | master 默认 join（[master.cpp:122](../src/yijinjing/practice/master.cpp#L122)，但对应 cached 的 CMD 通道，不是 master 自己写） |

### 5B. SYSTEM/service/cached/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB | cached（实际很少写 PUBLIC） | master 默认 join + 其他有 join PUBLIC 的进程 |
| 1 (SYNC) | `00000001.1.journal` | 1MB | cached（空） | master 默认 join |
| `UCM_C` = Cached 进程的 master_cmd pseudo uid | `{UCM_C:08x}.1.journal` | 1MB | master（SessionStart / TimeReset / RequestCachedDone） | cached（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_L` | `{U_L:08x}.1.journal` | 16MB（dest≠0，SYSTEM 走默认？——SYSTEM 类 dest!=0 默认 1MB，STRATEGY/TD 类 dest≠0 才 16MB） | cached（ledger 读 cached） | ledger（`request_read_from(now(), U_C)` → `reader_->join(cached_home, U_L)`） |

### 5C. SYSTEM/service/ledger/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB | ledger | master 默认 join |
| 1 (SYNC) | `00000001.1.journal` | 1MB | ledger（空） | master 默认 join |
| `UCM_L` | `{UCM_L:08x}.1.journal` | 1MB | master（命令通道） | ledger（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_C` | `{U_C:08x}.1.journal` | 1MB | ledger（`request_write_to(now(), U_C)`） | cached（RequestReadFrom 反向） |
| `U_D` | `{U_D:08x}.1.journal` | 1MB（ledger SYSTEM 类，不是 STRATEGY 类） | ledger（AutoClient `request_write_to`） | MD（ledger → MD 的 InstrumentKey 订阅请求） |
| `U_T` | `{U_T:08x}.1.journal` | 1MB | ledger（写 ResetBookRequest、订阅） | TD |
| `U_S` | `{U_S:08x}.1.journal` | 1MB | ledger（写对账、book reset） | strategy |

### 5D. MD/sim/sim/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | **128MB**（MD dest!=1） | MD（Quote / Trade / InstrumentKey / 自定义行情） | master 默认 join / ledger `reader_->join(MD,0)` / strategy `reader_->join(MD,0)` / cached 通过 inspect_channel（**不直接 join MD PUBLIC**，但 Channel 事件会 join 其他 pair） |
| 1 (SYNC) | `00000001.1.journal` | 1MB（MD dest==1） | MD（空，注册时默认开，但 MD 不发同步快照——没有持仓概念） | master 默认 join |
| `UCM_D` | `{UCM_D:08x}.1.journal` | 1MB | master（命令通道） | MD（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_L` | `{U_L:08x}.1.journal` | 1MB | MD（给 ledger 的私回报？MD 一般没有私回报，只发 PUBLIC。但通道会因 request_write_to 而开） | ledger（收到 `RequestWriteTo` 后 writer emplace，master join） |
| `U_S` | `{U_S:08x}.1.journal` | 1MB | MD（给 strategy 的私回报，同上） | strategy（同上） |

### 5E. TD/sim/sim/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB（TD dest==0） | TD（BrokerStateUpdate / Order 广播 / Trade 广播） | master 默认 join / ledger join / strategy join / cached 显式 join（[cached.cpp:192](../src/yijinjing/cache/cached.cpp#L192)） |
| 1 (SYNC) | `00000001.1.journal` | **16MB**（TD dest!=0） | TD（Asset 快照 / AssetMargin 快照 / Position 快照 / PositionEnd 快照 —— Trader 启动 `enable_asset_sync` 阶段写，之后切回 PUBLIC 增量）见 [trader.cpp:98-108](../src/wingchun/broker/trader.cpp#L98-L108) / bookkeeper 的 `fork<*>::SYNC` [bookkeeper.cpp:73-76](../src/wingchun/book/bookkeeper.cpp#L73-L76) | master 默认 join / ledger join SYNC / strategy join SYNC |
| `UCM_T` | `{UCM_T:08x}.1.journal` | 1MB | master（命令通道） | TD（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_L` | `{U_L:08x}.1.journal` | 16MB（TD dest!=0） | TD（给 ledger 的私订单回报） | ledger（`request_read_from(ledger→TD)` → master 建反向 write_to(TD, U_L)） |
| `U_S` | `{U_S:08x}.1.journal` | 16MB | TD（给 strategy 的私订单回报、成交、持仓） | strategy（同上） |
| `U_D`? No |  |  |  | MD/TD 之间一般不直接建通道。MD 读 PUBLIC，TD 读 OrderInput 来自 strategy |

### 5F. STRATEGY/sim/sim/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB | strategy（一般无业务写 PUBLIC，但会写 broker 相关数据） | master 默认 join / ledger join strategy PUBLIC |
| 1 (SYNC) | `00000001.1.journal` | **16MB**（STRATEGY dest!=0） | strategy（空，注册默认开） | master 默认 join / ledger join strategy SYNC |
| `UCM_S` | `{UCM_S:08x}.1.journal` | 1MB | master（命令通道） | strategy（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_D` | `{U_D:08x}.1.journal` | 16MB（STRATEGY dest!=0） | strategy（InstrumentKey 订阅请求 → MD 私通道） | MD（收到 RequestWriteTo） |
| `U_T` | `{U_T:08x}.1.journal` | 16MB | strategy（OrderInput 下单给 TD） | TD（收到 RequestWriteTo） |
| `U_L` | `{U_L:08x}.1.journal` | 16MB | strategy（给 ledger 的私回报） | ledger（AutoClient `request_read_from(strategy)`） |

### 5G. 4 个 master_cmd 伪 location（SYSTEM/master/{hex}/live）
每个进程都有**自己的 master_cmd 目录**（因为 master_cmd_location_ 是根据进程自身 uid 独立生成的：[hero.cpp:33](../src/yijinjing/practice/hero.cpp#L33)），即 6 个 × 各自 home uid 的 hex 名。

例如对 master_cmd_cached（hex=`fmt::format("{:08x}", U_C)`）目录 `SYSTEM/master/{U_C:08x}/live/`：

| dest_id | 文件 | 谁写 | 谁读 |
|---|---|---|---|
| `U_C` (cached home uid) | `{U_C:08x}.1.journal` | master（命令下发） | cached（apprentice base join） |
| 默认 0 / 1（注册 register_app 会默认开） | `00000000.1.journal` / `00000001.1.journal` | master（register_app `require_write_to(CMx, 0/1)` 虽然 master_cmd 不是真 app，但 register_app 中 `CMx` 会被 `try_add_location`，并由 `app_cmd_writer` 的 writer 对应 source=CMx，dest=Ux 这条——实际上只有 dest=Ux 这一条有数据） | |

**注意**：master_cmd_{hex} 是**单向**伪 location：master 写 dest=对应 app_uid，不写 PUBLIC/SYNC；但 `register_app` 中 `require_write_to(CMx, PUBLIC/SYNC/master_cmd_uid)` 会发起 3 条，所以也存在 0/1 页文件，只是为空。

---

## 六、最终 Channel 注册（{source, dest} 对）汇总表

所有 `Channel{source, dest}` 由 master 广播到 master PUBLIC（[master.cpp:296](../src/yijinjing/practice/master.cpp#L296)），可被所有 Channel::tag 订阅者 / cached / ledger 的 inspect_channel 使用。

### A. register_app 每个 app 默认开的 3 条 writer 不算 Channel
上面 3A 的 (X,PUBLIC), (X,SYNC), (X,CMX) 是通过 `require_write_to` 完成的，但它不发 `Channel` 事件（只有 `on_request_write_to`/`on_request_read_from` 发）。所以它们不计入 Channel map。

### B. App 之间显式建的 Channel

按“谁发起 request”汇总，每个 request 对应一个 Channel：

| # | source_id | dest_id | 发起方 | 触发 API | 调用点 |
|---|-----------|---------|--------|----------|--------|
| 1 | U_L (ledger) | U_C (cached) | ledger | `request_write_to(now(), U_C)` | [apprentice.cpp:96](../src/yijinjing/practice/apprentice.cpp#L96) |
| 2 | U_C (cached) | U_L (ledger) | ledger | `request_read_from(now(), U_C, now())` → master 反向建 `require_write_to(U_C, U_L)` | [apprentice.cpp:97](../src/yijinjing/practice/apprentice.cpp#L97) → [master.cpp:308](../src/yijinjing/practice/master.cpp#L308) |
| 3 | U_L | U_D (md) | ledger AutoClient MD connect | `request_write_to(U_D)` | [client.cpp:157](../src/wingchun/broker/client.cpp#L157) → [master.cpp:279-297](../src/yijinjing/practice/master.cpp#L279-L297) |
| 4 | U_S (strategy) | U_D | strategy PassiveClient MD connect | `request_write_to(U_D)` | [client.cpp:157](../src/wingchun/broker/client.cpp#L157)（strategy 内部调用 connect(MD Register)） |
| 5 | U_L | U_T (td) | ledger AutoClient TD connect | `request_write_to(U_T)` | [client.cpp:162](../src/wingchun/broker/client.cpp#L162) |
| 6 | U_S | U_T | strategy PassiveClient TD connect / `ensure_connect` | `request_write_to(U_T)` | [client.cpp:162](../src/wingchun/broker/client.cpp#L162) / [runtime.cpp:355-370](../src/wingchun/strategy/runtime.cpp#L355-L370) |
| 7 | U_T | U_L | ledger AutoClient TD connect | `request_read_from(U_T)` → master `require_write_to(U_T, U_L)` | [client.cpp:163](../src/wingchun/broker/client.cpp#L163) → [master.cpp:308](../src/yijinjing/practice/master.cpp#L308) |
| 8 | U_T | U_S | strategy PassiveClient TD connect | `request_read_from(U_T)` → master `require_write_to(U_T, U_S)` | [client.cpp:163](../src/wingchun/broker/client.cpp#L163) |
| 9 | U_L | U_S (strategy) | ledger AutoClient STRATEGY connect | `request_write_to(U_S)` | [client.cpp:169](../src/wingchun/broker/client.cpp#L169) |
| 10 | U_S | U_L | ledger AutoClient STRATEGY connect | `request_read_from(U_S)` → master `require_write_to(U_S, U_L)` | [client.cpp:170](../src/wingchun/broker/client.cpp#L170) |

**合计：10 条显式 Channel**（都会有 `Channel` 事件广播）。加上每个 app 默认的 PUBLIC/SYNC/CMD 三条（不计入 Channel map，但有独立 journal 文件），系统运行时总共 **28 条左右独立的 journal 文件簇**（按 (source, dest_id) 对），每簇包含若干连续 page 文件（写满自动翻页）。

> **注意**：strategy101.h 的 `RuntimeContext::subscribe("sim", {"600000"}, "SSE")` 会对 MD 再触发一次 `ensure_connect` → `send_instrument_keys`。但这只是在已建立的 Channel (U_S→U_D) 上写 `InstrumentKey` 帧，不会新建 Channel，也不会新建 journal 文件；MD 的 Ready 后，strategy PassiveClient 的 `renew()` 会用已打开的 `writer(U_D)` 重发 InstrumentKey，仍然复用已建通道。


---

每个 journal 是固定大小的吗

## 每个 journal 是固定大小的吗？

**结论：每个 **page**（即单个 `{dest_id:08x}.{page_id}.journal` 文件）的大小是固定的；但**每个 journal 数据流**由无限多个 page 串起来，整体不限大小。**

换句话说，你在文件系统上看到的每一个 `.journal` 文件，一旦创建，其大小就不会再变；但写满后会自动翻页（生成下一个 page 文件），所以整条数据流在逻辑上是无限的。

---

### 一、单个 page 文件大小是固定的

`find_page_size` ([page.h:84-94](../include/kungfu/yijinjing/journal/page.h#L84-L94)) 预先按 (category, dest_id) 决定每个 page 的大小：

| 情况 | page 大小 | 说明 |
|---|---|---|
| `category == MD` 且 `dest_id != 1`（即 MD 的 PUBLIC 通道） | **128 MB** | 行情量大 |
| `category == TD or STRATEGY` 且 `dest_id != 0`（即 TD / strategy 的 SYNC 与私有通道） | **16 MB** | 订单/持仓中量 |
| 其他所有情况（SYSTEM 类、TD/strategy 的 PUBLIC、MD 的 SYNC 等） | **1 MB** | 小流量 |

page 大小在文件创建时就写入到 `page_header.page_size`（[page.cpp:44](../src/yijinjing/journal/page.cpp#L44)），后续加载时会校验**严格一致**，不匹配直接抛 `journal_error`（[page.cpp:57-62](../src/yijinjing/journal/page.cpp#L57-L62)），**不可能动态改变**：

```cpp
if (header->page_size != page_size) {
  throw journal_error(
      fmt::format("page size mismatch, required {}, found {}, ...", page_size, s, ...));
}
```

mmap 时也是按这个固定大小创建映射的 `os::load_mmap_buffer(path, page_size, is_writing, lazy)`（[page.cpp:31](../src/yijinjing/journal/page.cpp#L31)）。

---

### 二、翻页机制：写满自动生成下一个 page

writer 在 `open_frame` 时做容量检查（[writer.cpp:36-38](../src/yijinjing/journal/writer.cpp#L36-L38)）：

```cpp
if (journal_.current_frame()->address() + sizeof(frame_header) + data_length
    >= journal_.page_->address_border()) {    // address_border = mmap 基址 + page_size - sizeof(frame_header)
  close_page(trigger_time);                   // 当前页写满 → 翻页
}
```

`close_page` 做两件事（[writer.cpp:105-120](../src/yijinjing/journal/writer.cpp#L105-L120)）：

1. `journal_.load_next_page()` → `load_page(page_->get_page_id() + 1)`（[journal.cpp:46](../src/yijinjing/journal/journal.cpp#L46) / [journal.cpp:38-44](../src/yijinjing/journal/journal.cpp#L38-L44)）→ 新建下一个 page 文件，大小同样由 `find_page_size` 决定（与上一页同规格）。
2. 在**上一页**的末尾追加一个 `PageEnd` 标记帧（`msg_type == PageEnd::tag`），让 reader 读到这里就知道该跳到下一页。

reader 侧的 `journal::next` 正是通过检测 `PageEnd` 自动翻页的（[journal.cpp:17-25](../src/yijinjing/journal/journal.cpp#L17-L25)）：

```cpp
void journal::next() {
  if (frame_->msg_type() == longfist::types::PageEnd::tag) {
    load_next_page();    // 读到 PageEnd → 跳到下一个 journal 文件
  } else {
    frame_->move_to_next();
  }
}
```

新文件的命名是 `{dest_id:08x}.{page_id+1}.journal`（[page.cpp:67-70](../src/yijinjing/journal/page.cpp#L67-L70)），例如：
- 第 1 页：`00000000.1.journal`
- 第 2 页：`00000000.2.journal`
- 第 3 页：`00000000.3.journal`
- ...依此类推，直到硬盘用完或进程结束。

---

### 三、关于单帧上限

注意 `open_frame` 开头的断言（[writer.cpp:29](../src/yijinjing/journal/writer.cpp#L29)）：

```cpp
assert(sizeof(frame_header) + data_length + sizeof(frame_header) <= journal_.page_->get_page_size());
```

单帧数据**不能超过一页减去两个 frame header**，否则直接触发断言失败。所以对于不同 category 的通道，单条消息的最大 data_length 是：

| 通道规格 | page_size | 单条消息最大 data_length |
|---|---|---|
| MD PUBLIC（128MB） | 134,217,728 B | ~134,217,664 B（减去 2×32 = 64 B header） |
| TD/STRATEGY SYNC 或 私有（16MB） | 16,777,216 B | ~16,777,152 B |
| 其余（1MB） | 1,048,576 B | ~1,048,512 B |

如果真需要写超大帧（比如回放一批历史行情），要么使用 MD 的 128MB PUBLIC 通道，要么分帧。**这也是为什么 MD 的 PUBLIC 给 128MB 大页的原因**（行情数据量大，同时要避免频繁翻页）。

---

### 四、总结

| 问题 | 答案 |
|---|---|
| **单个 `.journal` 文件（page）** | ✅ **大小固定**，由 (category, dest_id) 在 `find_page_size` 中查表决定，mmap 时创建，文件头里写入 `page_size`，后续加载做严格校验，永不扩大/缩小。 |
| **一条 journal 数据流（source, dest_id）的总容量** | ❌ **不固定、不限**。写满就生成下一个 `.journal`（page_id 递增 + PageEnd 标记），reader 读到 PageEnd 自动跳到下一页。理论上是无限的 append-only 序列。 |
| **一条消息（frame）能有多大** | 受所在通道 page_size 限制（单帧 ≤ page_size − 2 个 frame_header）。MD PUBLIC 最大 ~128MB/帧，1MB 通道最大 ~1MB/帧。 |

---

**User**

journal  和 page 的关系

**Assistant**

## journal 和 page 的关系

一句话：**`page` 是磁盘上的单个 `.journal` 文件（内存映射的固定大小页），`journal` 是对「多个连续 page 串成的一条逻辑数据流」的抽象**。`journal` 类内部每次只加载一个 `page`，读到/写到 PageEnd 就切到下一个 page。

---

### 一、类关系一览

| 类 | 定义位置 | 表示什么 | 是否映射到一个具体文件 |
|---|---|---|---|
| `page` | [page.h](../include/kungfu/yijinjing/journal/page.h#L19-L82) / [page.cpp](../src/yijinjing/journal/page.cpp) | **单个物理文件**：`{location}/journal/{dest_id:08x}.{page_id}.journal` 的 mmap 视图 | ✅ 1:1 对应一个文件 |
| `journal` | [journal.h](../include/kungfu/yijinjing/journal/journal.h#L19-L66) / [journal.cpp](../src/yijinjing/journal/journal.cpp) | **一条逻辑数据流**：`(source_location, dest_id)` 这对唯一标识下的所有 page 序列 | ❌ 可以跨 N 个文件 |
| `frame` | [frame.h](../include/kungfu/yijinjing/journal/frame.h) | **一条消息**：page 里面的一个数据帧（header + payload） | N/A |
| `reader` / `writer` | [journal.h](../include/kungfu/yijinjing/journal/journal.h#L68-L212) | **读写入口**：持有一个 `journal` 对象，调用 `next()` 推进游标，`open_frame/close_frame` 写入数据 | N/A |

`journal` 类里的关键成员（[journal.h:50-56](../include/kungfu/yijinjing/journal/journal.h#L50-L56)）：

```cpp
class journal {
  const data::location_ptr location_;   // 写方的 source location（如 MD/sim/sim/live）
  const uint32_t dest_id_;              // 目标通道号（0=PUBLIC, 1=SYNC, 其他=私有的 dest uid）
  // ↑ 这两者组合起来唯一标识一条逻辑 journal 数据流

  page_ptr page_;                       // ← 当前加载的 page（只是这条流中的一页）
  frame_ptr frame_;                     // 当前位置的帧
  uint64_t page_frame_nb_;              // 当前页里已经读/写了多少帧
};
```

> 所以：**`journal` 对象内部并没有"所有 pages 的列表"**，它只知道 (location, dest_id) + 当前 page，需要下一页的时候就用 `page_id + 1` 再 `load` 一个。

---

### 二、page 是物理层：文件 → mmap

`page::load`（[page.cpp:27-65](../src/yijinjing/journal/page.cpp#L27-L65)）负责把一个磁盘文件变成一块 mmap 的内存：

```
[磁盘] {locator JOURNAL dir}/{category}/{group}/{name}/{mode}/{dest_id:08x}.{page_id}.journal
   │
   │ os::load_mmap_buffer(path, page_size, is_writing, lazy)
   ▼
[内存] 一块 page_size 字节的 mmap 区域
       ├─开头: page_header（version, page_size, last_frame_position 等固定字段）
       └─剩余: 连续存放的 N 个 frame（每帧 = frame_header + payload）
```

文件路径由 `get_page_path` 拼出（[page.cpp:67-70](../src/yijinjing/journal/page.cpp#L67-L70)）：

```cpp
auto page_name = fmt::format("{:08x}.{}", dest_id, page_id);
return location->locator->layout_file(location, layout::JOURNAL, page_name);
```

即：**同一个 (location, dest_id) 产生的不同 page，`dest_id:08x` 前缀相同，只有 `page_id` 递增。**

| 文件名 | 含义 |
|---|---|
| `00000000.1.journal` | `(Lx, PUBLIC=0)` 数据流的第 1 页 |
| `00000000.2.journal` | `(Lx, PUBLIC=0)` 数据流的第 2 页 |
| `00000001.1.journal` | `(Lx, SYNC=1)` 数据流的第 1 页 |
| `deadbeef.1.journal` | `(Lx, dest=0xdeadbeef)` 数据流的第 1 页（私有通道） |
| `deadbeef.2.journal` | `(Lx, dest=0xdeadbeef)` 数据流的第 2 页 |
| ... | ... |

page 还提供了一些"边界/时间定位"方法，比如：
- `address_border()` = `address() + page_size - sizeof(frame_header)`（[page.h:42-43](../include/kungfu/yijinjing/journal/page.h#L42-L43)）——写入不可越过
- `begin_time()` = 第一帧的 `gen_time`（[page.h:32-35](../include/kungfu/yijinjing/journal/page.h#L32-L35)）
- `end_time()` = 最后一帧的 `gen_time`（[page.h:36-39](../include/kungfu/yijinjing/journal/page.h#L36-L39)）
- `is_full()` = 当前页已经写到 border 外了吗？（[page.h:50-55](../include/kungfu/yijinjing/journal/page.h#L50-L55)）

`page::find_page_id`（[page.cpp:72-86](../src/yijinjing/journal/page.cpp#L72-L86)）是按时间定位的入口：列出某个 (location, dest_id) 下所有已有 page_id，从后往前比较 `begin_time < target_time`，找到应该从哪一页开始 seek。这就是 `journal::seek_to_time` 能"跳回到某个时间点"的基础。

---

### 三、journal 是逻辑层：在 page 之间做"翻页"

`journal` 并不直接暴露文件，它对外呈现的是"一条无限长的帧流"。核心就是两个操作：

#### 1. `load_page(page_id)` — 切到某一页

```cpp
void journal::load_page(int page_id) {                     // [journal.cpp:38-44]
  if (page_ == nullptr or page_->get_page_id() != page_id) {
    page_ = page::load(location_, dest_id_, page_id, is_writing_, lazy_);  // ★ 调用 page::load
  }
  frame_->set_address(page_->first_frame_address());        // 把当前帧指针指到新页的第一帧
  page_frame_nb_ = 0u;                                      // 重置页内计数
}
```

#### 2. `load_next_page()` — 翻到下一页（`page_id + 1`）

```cpp
void journal::load_next_page() { load_page(page_->get_page_id() + 1); }  // [journal.cpp:46]
```

#### 3. `next()` — 推进一帧，遇到 PageEnd 自动翻页

```cpp
void journal::next() {                                     // [journal.cpp:17-25]
  if (frame_->msg_type() == longfist::types::PageEnd::tag) {
    load_next_page();                                      // ★ 到页尾 → 切到下一个 journal 文件
  } else {
    frame_->move_to_next();                                // 页内 → 指针直接前进，跳到下一帧的 header
    page_frame_nb_++;
  }
}
```

**这里的 PageEnd 标记**是 writer 在翻页前写在上一页的末尾的（[writer.cpp:105-120](../src/yijinjing/journal/writer.cpp#L105-L120)）：

```cpp
void writer::close_page(int64_t trigger_time) {
  page_ptr last_page = journal_.page_;
  journal_.load_next_page();                               // 先加载下一页（可能是新创建的空文件）

  frame last_page_frame;                                   // 再回写 PageEnd 到上一页末尾
  last_page_frame.set_address(last_page->last_frame_address());
  last_page_frame.move_to_next();
  last_page_frame.set_msg_type(longfist::types::PageEnd::tag);
  last_page_frame.set_gen_time(time::now_in_nano());
  ...
}
```

`PageEnd` 就像电影胶卷片尾的"未完待续"标记——reader 看到它就知道这卷胶卷到头了，该换下一卷（page_id+1）。

#### 4. `seek_to_time(nanotime)` — 在多页流中按时间跳转

```cpp
void journal::seek_to_time(int64_t nanotime) {             // [journal.cpp:27-36]
  int page_id = page::find_page_id(location_, dest_id_, nanotime);  // 1. 找到时间所在的 page
  load_page(page_id);                                                // 2. 加载那一页
  while (page_->is_full() && page_->end_time() <= nanotime) {       // 3. 若该页全写完且时间还不够，再往后翻
    load_next_page();
  }
  while (frame_->has_data() && frame_->gen_time() <= nanotime) {    // 4. 在目标页里逐帧前进到目标时间
    next();
  }
}
```

---

### 四、reader/writer 作为最上层的门面

`reader` 和 `writer` 都只持有一个 `journal` 对象（而不是 page）。它们的工作流是：

- **writer**（[writer.cpp](../src/yijinjing/journal/writer.cpp)）：
  1. `open_frame` → 用 `address_border` 检查当前页写不写得下 → 不够就 `close_page`（翻页）
  2. 在当前页的 frame 位置写 header + payload
  3. `close_frame` → 更新 `last_frame_position` → `journal_.next()`

- **reader**（[reader.cpp](../src/yijinjing/journal/reader.cpp)）：
  1. `join(location, dest_id)` → 在 `journals_` map 里 emplace 一个 `journal(location, dest_id, false, lazy_)`
  2. `data_available()` → `sort()` → 从所有 journal 的当前 frame 中找 `gen_time` 最小的那个（`current_`）
  3. `next()` → `current_->next()` → 如果命中 PageEnd，journal 自动翻页

所以对上层来说，**完全看不到 page 的存在**——reader/writer 只和 `journal`（一条流）与 `frame`（一条消息）交互，`page` 的创建、mmap、翻页、校验、定位都在 journal/page 层自动完成。

---

### 五、类比总结

| 概念 | 书本类比 | 文件类比 |
|---|---|---|
| **`journal`（数据流）** | 一本**书**（《MD PUBLIC 行情日志》）——可以无限厚，可以从任意章节开始翻 | 多个 page 文件串联的"卷" |
| **`page`** | **书中的一张页**——固定大小（书宽×书高），每张页写满就翻下一张 | 单个 `{dest_id:08x}.{page_id}.journal` 文件，固定 1MB / 16MB / 128MB |
| **`frame`** | **页上的一行字**——长短不一，但一张页写不下最大长度的一行（单帧不能超过 page-2header） | 单个数据帧（frame_header + payload） |
| **`PageEnd` 标记** | **页尾的"见下页"**——读者看到就知道该翻页了 | 上一页最后一帧的 msg_type = PageEnd::tag |
| **`reader::sort()`** | **同时看多本书，挑出页码最小（时间最早）那本翻一页** | 多个 (location, dest_id) 流中按 gen_time 取最小当前帧 |
| **`seek_to_time`** | **查书目 → 翻到那章 → 在章里逐行找到要的位置** | find_page_id 定位页 → load_page → 逐帧 next |

关键点：**journal 是"逻辑上的一条连续流"，page 是"物理上切分的一个个固定大小的文件"**，两者靠 `PageEnd` 标记 + `page_id 递增约定`无缝衔接，上层业务完全感知不到"我其实已经切换到另一个 mmap 文件了"。

---

page_id 是怎么初始化的？

## page_id 是怎么初始化的？

**核心结论：第一个 page 的 id 恒为 `1`（不是 0），后续每翻一页 `+1`。初始化时通过扫描磁盘上已存在的 `.journal` 文件来决定从哪一页开始。**

---

### 一、入口：writer / reader 构造时触发

**writer** 构造函数（[writer.cpp:14-19](../src/yijinjing/journal/writer.cpp#L14-L19)）：

```cpp
writer::writer(const data::location_ptr &location, uint32_t dest_id, bool lazy, publisher_ptr publisher)
    : ... , journal_(location, dest_id, true, lazy), ... {
  journal_.seek_to_time(time::now_in_nano());   // ★ 用当前时间定位
}
```

**reader** 的 `join` 方法（[reader.cpp:10-19](../src/yijinjing/journal/reader.cpp#L10-L19)）：

```cpp
void reader::join(const data::location_ptr &location, uint32_t dest_id, const int64_t from_time) {
  auto key = ...;
  auto result = journals_.try_emplace(key, location, dest_id, false, lazy_);
  if (result.second) {
    journals_.at(key).seek_to_time(from_time);   // ★ 用 from_time 定位
  }
  ...
}
```

两者都走到同一个入口：**`journal::seek_to_time`**。

---

### 二、seek_to_time：调用 find_page_id 确定起始 page_id

[journal.cpp:27-36](../src/yijinjing/journal/journal.cpp#L27-L36)：

```cpp
void journal::seek_to_time(int64_t nanotime) {
  int page_id = page::find_page_id(location_, dest_id_, nanotime);  // ① 算出 page_id
  load_page(page_id);                                                // ② 加载该页
  while (page_->is_full() && page_->end_time() <= nanotime) {        // ③ 若写满且时间还不到，继续翻
    load_next_page();
  }
  while (frame_->has_data() && frame_->gen_time() <= nanotime) {     // ④ 页内逐帧前进
    next();
  }
}
```

---

### 三、find_page_id：扫描磁盘文件决定 page_id

[page.cpp:72-86](../src/yijinjing/journal/page.cpp#L72-L86)：

```cpp
uint32_t page::find_page_id(const data::location_ptr &location, uint32_t dest_id, int64_t time) {
  std::vector<uint32_t> page_ids = location->locator->list_page_id(location, dest_id);
  if (page_ids.empty()) {
    return 1;                    // ★★★ 没有任何文件 → 从第 1 页开始（不是 0！）
  }
  if (time == 0) {
    return page_ids.front();     // time=0 → 从最早的那页开始
  }
  for (int i = static_cast<int>(page_ids.size()) - 1; i >= 0; i--) {
    if (page::load(location, dest_id, page_ids[i], false, true)->begin_time() < time) {
      return page_ids[i];        // ★ 从后往前找：第一个 begin_time < 目标时间的页
    }
  }
  return page_ids.front();       // 都不满足 → 回到第一页
}
```

三种结果：

| 场景 | 返回值 |
|---|---|
| 目录下没有 `.journal` 文件（全新 writer 首次启动） | **`1`** |
| 有文件，`time == 0`（reader 从头读） | `page_ids.front()`（最小 page_id） |
| 有文件，`time > 0`（writer 续写 / reader 跳转） | **从后往前**找到第一个 `begin_time < time` 的 page_id |

---

### 四、list_page_id：扫描磁盘文件解析 page_id

[locator.cpp:112-125](../src/yijinjing/io/locator.cpp#L112-L125)：

```cpp
std::vector<uint32_t> locator::list_page_id(const location_ptr &location, uint32_t dest_id) const {
  std::vector<uint32_t> result = {};
  auto dest_id_str = fmt::format("{:08x}", dest_id);                    // e.g. "00000000"
  auto dir = fs::path(layout_dir(location, es::layout::JOURNAL));       // e.g. ".../journal/md/sim/sim/live/"
  for (auto &it : fs::recursive_directory_iterator(dir)) {
    auto basename = it.path().stem();                                    // "00000000.1" → stem="00000000"
    if (it.is_regular_file() and it.path().extension() == ".journal"
        and basename.stem() == dest_id_str) {                            // 匹配 dest_id 前缀
      auto index = std::atoi(basename.extension().string().c_str() + 1); // ".1" → 1
      result.push_back(index);
    }
  }
  std::sort(result.begin(), result.end());                               // 排序后返回
  return result;
}
```

文件名格式 `{dest_id:08x}.{page_id}.journal` 的拆解：

```
文件名:  00000000.3.journal
          ├────────┘ ├┘
          dest_id    page_id
          (8 hex)    (十进制整数)
```

`basename.stem()` 取得 `00000000`，`basename.extension()` 取得 `.3`，`c_str() + 1` 跳过 `.` 后 `atoi` 得到 `3`。

---

### 五、page::load：page_id 确定后创建/打开文件

[page.cpp:27-65](../src/yijinjing/journal/page.cpp#L27-L65)：

```cpp
page_ptr page::load(const data::location_ptr &location, uint32_t dest_id, uint32_t page_id,
                    bool is_writing, bool lazy) {
  uint32_t page_size = find_page_size(location, dest_id);
  std::string path = get_page_path(location, dest_id, page_id);         // 拼出文件路径
  uintptr_t address = os::load_mmap_buffer(path, page_size, is_writing, lazy);  // mmap

  if (address == 0) {
    throw journal_error("unable to load page for " + path);
  }

  page_header *header = reinterpret_cast<page_header *>(address);
  if (header->last_frame_position == 0) {                                // ★ 新文件（全零）
    header->version = __JOURNAL_VERSION__;
    header->page_header_length = sizeof(page_header);
    header->page_size = page_size;
    header->frame_header_length = sizeof(frame_header);
    header->last_frame_position = header->page_header_length;            // 写头指向第一个可写位置
  }
  // ... 校验 version / page_size 等 ...
  return std::shared_ptr<page>(new page(location, dest_id, page_id, page_size, lazy, address));
}
```

文件路径由 `get_page_path` 拼出（[page.cpp:67-70](../src/yijinjing/journal/page.cpp#L67-L70)）：

```cpp
std::string page::get_page_path(const data::location_ptr &location, uint32_t dest_id, uint32_t page_id) {
  auto page_name = fmt::format("{:08x}.{}", dest_id, page_id);   // e.g. "00000000.1"
  return location->locator->layout_file(location, layout::JOURNAL, page_name);
}
```

`layout_file`（[locator.cpp:91-94](../src/yijinjing/io/locator.cpp#L91-L94)）最终拼出：

```
{root}/journal/{category}/{group}/{name}/{mode}/{dest_id:08x}.{page_id}.journal
```

例如：`/var/kungfu/journal/md/sim/sim/live/00000000.1.journal`

对于 writer（`is_writing=true`），`os::load_mmap_buffer` 会以读写方式打开文件——**文件不存在时自动创建**一个 `page_size` 大小的空文件，然后 mmap。这就是新 page 的"创建"动作。

---

### 六、翻页：page_id 严格 +1 递增

当 writer 写满当前页时（[writer.cpp:36-38](../src/yijinjing/journal/writer.cpp#L36-L38)），调用 `close_page`（[writer.cpp:105-120](../src/yijinjing/journal/writer.cpp#L105-L120)）：

```cpp
void writer::close_page(int64_t trigger_time) {
  page_ptr last_page = journal_.page_;
  journal_.load_next_page();   // ★ load_page(page_->get_page_id() + 1)
  // ... 在上一页末尾写 PageEnd 标记 ...
}
```

`load_next_page`（[journal.cpp:46](../src/yijinjing/journal/journal.cpp#L46)）：

```cpp
void journal::load_next_page() { load_page(page_->get_page_id() + 1); }
```

**严格 `+1`**，不会跳号。如果 `page_id + 1` 的文件不存在，writer 的 `page::load` 会自动创建它。

---

### 七、完整初始化流程图

以一个全新的 MD 进程首次启动为例（`md/sim/sim/live` 的 PUBLIC 通道 `dest_id=0`）：

```
writer 构造
  │
  ▼
seek_to_time(now)
  │
  ├──► find_page_id(md_location, 0, now)
  │      │
  │      ├──► list_page_id(md_location, 0)
  │      │      │
  │      │      ▼ 扫描 /var/kungfu/journal/md/sim/sim/live/
  │      │      │  找 00000000.*.journal 文件
  │      │      │
  │      │      ▼ 首次启动 → 目录为空 → 返回 []
  │      │
  │      ▼ page_ids.empty() → return 1   ← ★ 起始 page_id = 1
  │
  ├──► load_page(1)
  │      │
  │      ├──► get_page_path → /var/kungfu/journal/md/sim/sim/live/00000000.1.journal
  │      ├──► find_page_size → 128MB (MD PUBLIC)
  │      ├──► os::load_mmap_buffer(path, 128MB, is_writing=true, lazy)
  │      │      │
  │      │      ▼ 文件不存在 → 创建 128MB 空文件 → mmap (MAP_SHARED, PROT_READ|PROT_WRITE)
  │      │
  │      └──► header->last_frame_position == 0 → 初始化 page_header
  │             version = __JOURNAL_VERSION__
  │             page_size = 128MB
  │             last_frame_position = sizeof(page_header)   ← 指向第一个可写帧
  │
  ├──► page_->is_full()? → 否（新页）→ 跳过翻页循环
  └──► frame->has_data()? → 否（新页无帧）→ 跳过帧前进循环
       │
       ▼
  writer 就绪，当前 page_id=1，frame 指向页首，等待第一次 open_frame
```

之后每次写满 → `close_page` → `load_page(2)` → 创建 `00000000.2.journal` → ... → `load_page(3)` → ...

如果进程重启，`find_page_id` 会扫到 `[1, 2, 3, ...]`，从后往前找到 `begin_time < now` 的那一页（比如 page 5），`load_page(5)`，然后在页内逐帧前进到最后写入位置，继续追加写。

---

### 八、一句话总结

| 问题 | 答案 |
|---|---|
| 首个 page_id | **`1`**（硬编码于 `find_page_id` 的 `page_ids.empty()` 分支） |
| 后续 page_id | **严格 `+1`** 递增（`load_next_page` → `load_page(current + 1)`） |
| 初始化依据 | 扫描磁盘上已存在的 `{dest_id:08x}.*.journal` 文件，按 `begin_time` 定位到"最后写入的那一页"续写；无文件则从 1 开始 |
| page_id 的存储 | 不存在内存索引里——**page_id 编码在文件名中**，每次通过 `list_page_id` 扫描目录恢复 |
| 文件创建时机 | writer 的 `page::load` 以 `is_writing=true` 调用 `os::load_mmap_buffer` 时，文件不存在则自动创建 |

---

