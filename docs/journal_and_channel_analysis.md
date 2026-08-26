# Kungfu 服务启动与 Journal/Channel 机制分析（基于 supervisord.conf）

本文档基于 [supervisord.conf](../config/supervisord.conf) 的默认配置，**完整**分析系统启动过程中各组件的 Location 身份、Journal 文件分配、Channel（通道）建立流程，以及最终稳定运行时的 Journal 文件清单与 Channel 汇总表。

所有 `uid` 的计算规则：`MurmurHash3_x86_32(uname_bytes, length, seed=42)`，其中 uname 格式 `{category}/{group}/{name}/{mode}`（定义于 [common.h](../include/kungfu/yijinjing/common.h) 的 `location` 构造函数，实现在 [hash.cpp](../src/yijinjing/util/hash.cpp)）。Journal 文件格式：`{dest_id:08x}.{page_id}.journal`。

---

## 一、6 个进程的 Location 身份表

根据 `supervisord.conf` 的启动顺序（priority 从低到高）：

| # | 进程 | 二进制 | category | group | name | mode† | 示例 uname |
|---|------|--------|----------|-------|------|-------|-------------|
| M | master | `kf_master` | SYSTEM | master | master | LIVE | `system/master/master/live` |
| C | cached | `kf_cached` | SYSTEM | service | cached | LIVE | `system/service/cached/live` |
| L | ledger | `kf_ledger` | SYSTEM | service | ledger | LIVE | `system/service/ledger/live` |
| D | md_sim | `kf_md` | **MD** | sim | sim | **LIVE**§ | `md/sim/sim/live` |
| T | td_sim | `kf_td` | **TD** | sim | sim | **LIVE**§ | `td/sim/sim/live` |
| S | strategy | `kf_strategy` | **STRATEGY** | sim | sim | LIVE | `strategy/sim/sim/live` |

> † `supervisord.conf` 启动时未传 `--mode`，所有进程默认 `mode=LIVE`。
> § md/td 构造 home 时 hardcode `mode::LIVE`，忽略 `--mode`。

同时，每个进程还内置 **4 个系统级 known location**：
- `master_home_location_` = `SYSTEM/master/master/live`
- `master_cmd_location_` = `SYSTEM/master/{:08x}/live`（自己 uid 的 8 位 hex，每个进程独立）
- `cached_home_location_` = `SYSTEM/service/cached/live`
- `ledger_home_location_` = `SYSTEM/service/ledger/live`

全部 `SYSTEM` 类 + `mode::LIVE`，构造于 [src/yijinjing/practice/hero.cpp:30-45](../src/yijinjing/practice/hero.cpp#L30-L45)。

---

## 二、Master 启动（priority=10，先于所有 app）

Master 构造时直接开一条 writer（[src/yijinjing/practice/master.cpp:33-36](../src/yijinjing/practice/master.cpp#L33-L36)）：

### Master 自己持有的 writer

| dest_id | 含义 | 代码位置 |
|---------|------|----------|
| **PUBLIC = 0** | master 对整个系统广播（Location / Register / Channel / Band / Deregister / TradingDay 等） | [master.cpp:35](../src/yijinjing/practice/master.cpp#L35) |

### Master 自己 join 的 reader

| source_location | dest_id | 含义 |
|-----------------|---------|------|
| `master_home` 自身 | PUBLIC | 读自己写的 PUBLIC，基类 apprentice 自动完成 |

Master 启动后，会预先加载之前持久化的 Location 和 Config 记录（[master.cpp:25-31](../src/yijinjing/practice/master.cpp#L25-31)），为 Session 回放做准备。

---

## 三、每个 App 注册时 Master 给它开的默认通道（`register_app`）

每个 app 注册都会触发 [master.cpp:90-142](../src/yijinjing/practice/master.cpp#L90-142) 的 `register_app`，为每个 app 统一建立 3 条 dest 的 journal + 3 对 Channel。以 "App X（home uid = Ux）" 为例，其 home location = Lx，注册瞬间生成的 master_cmd 伪 location uname = `system/master/{Ux:08x}/live`，记为 CMx，其 uid = Ucx。

### 3A. Master 给 App X 分配的 3 条 writer（source=App X）

| writer 的 dest_id | 对应 journal 文件 (在 Lx 下) | Page 大小（MD/TD/STRATEGY/SYSTEM） | 用途 |
|---|---|---|---|
| **PUBLIC = 0** | `Lx/00000000.1.journal`、`00000000.2.journal`... | MD=128MB / TD=1MB / STRATEGY=1MB / SYSTEM=1MB | 广播给所有消费者的公共数据（Quote / BrokerStateUpdate / Channel 广播 / 订单状态 等） |
| **SYNC = 1** | `Lx/00000001.1.journal`、`00000001.2.journal`... | MD=1MB / TD=16MB / STRATEGY=16MB / SYSTEM=1MB | 只对 TD / STRATEGY 有实际数据：Asset / Position / AssetMargin / PositionEnd 等账册全量快照 |
| **Ucx** (master_cmd 伪 location uid) | `Lx/{Ucx:08x}.1.journal`... | 1MB | master 给 App X 的一对一命令通道：Register 回执 / SessionStart / TimeReset / TradingDay / RequestStart / RequestCachedDone / 注册信息回灌 / channels / bands / locations |

> 注册流程里由 `require_write_to(..., Lx.uid, {PUBLIC|SYNC|Ucx})` 发出（[master.cpp:130-132](../src/yijinjing/practice/master.cpp#L130-132)），App X 收到 `RequestWriteTo` 事件后在自己的 `writers_` map 里 emplace 对应 writer（[apprentice.cpp:255-259](../src/yijinjing/practice/apprentice.cpp#L255-259)）。

### 3B. Master 作为 reader join 这 3 条

| source | dest_id | 代码位置 |
|--------|---------|----------|
| `Lx` (App X home) | PUBLIC | [master.cpp:120](../src/yijinjing/practice/master.cpp#L120) |
| `Lx` | SYNC | [master.cpp:121](../src/yijinjing/practice/master.cpp#L121) |
| `Lx` | `Ucx` (master_cmd) | [master.cpp:122](../src/yijinjing/practice/master.cpp#L122) |

### 3C. Master 同时给 App X 开一条反向 writer（master_cmd → App X）

| writer 的 source | dest_id | 文件路径 | 用途 |
|---|---|---|---|
| `CMx` (master_cmd location) | **Ux**（App X 自己的 uid） | `CMx/{Ux:08x}.{page}.journal` | App X 通过 master_cmd 发回给 master 的"请求回执"通道。由 `open_writer_at(CMx, Ux)` 打开，在 [master.cpp:111](../src/yijinjing/practice/master.cpp#L111)。 |

App X 在基类 apprentice::react 的 Register 回调中会 `reader_->join(CMx, Ux)`（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)），接收 master 下达到自己的命令。

### 3D. Master 广播到 PUBLIC 的 2 条事件（所有 app 都会收到）

1. `Location(Lx)`：Lx 的身份广播（[master.cpp:127](../src/yijinjing/practice/master.cpp#L127)）
2. `Register(Lx info)`：注册信息广播（[master.cpp:128](../src/yijinjing/practice/master.cpp#L128)）

---

## 四、按注册顺序的完整 Channel 清单（Channel = source→dest）

Channel 注册时 master 都会写一条 `Channel{source_id,dest_id}` 到 **master PUBLIC**（见 [master.cpp:292-296](../src/yijinjing/practice/master.cpp#L292-L296) / [master.cpp:310-314](../src/yijinjing/practice/master.cpp#L310-L314)）。

符号说明：
- U_M = master uid, U_C = cached uid, U_L = ledger uid, U_D = md(sim/sim) uid, U_T = td(sim/sim) uid, U_S = strategy(sim/sim) uid
- CMX = master_cmd_伪X 的 uid（`system/master/{UX:08x}/live` 的 hash）

### 阶段 1：cached 注册（priority=15，第 2 个进程）

默认 3 条 (C,PUBLIC), (C,SYNC), (C,CMC)。

| Channel (source → dest) | 由谁触发 | 代码路径 | 备注 |
|---|---|---|---|
| **M → CM_C**（master_cmd_cached 的 writer，作为 master→cached 命令入口） | 注册时 `open_writer_at(CM_C, U_C)` 返回 | [master.cpp:111](../src/yijinjing/practice/master.cpp#L111) | master_cmd 本身是伪 location，这里开的是 CM_C 目录下的 `{U_C:08x}.journal` |
| **M → C** (master home → cached private) | apprentice 基类在 `expect_start` 之前会从 master 读取 **RequestStart**（不需要 Channel，直接在 reader join master PUBLIC） | [apprentice.cpp:296](../src/yijinjing/practice/apprentice.cpp#L296) | 非显式 Channel，是基类默认 join master PUBLIC |
| **cached 自己读 (CM_C → U_C)**（master 下达给 cached 进程的私命令） | `reader_->join(CM_C, U_C, begin_time)` | [apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192) | 基类必建关系 |

Cached 特有行为：
- `register_trigger_listen_public`（[cached.cpp:183-195](../src/yijinjing/cache/cached.cpp#L183-195)）对 TD 的 Register 事件直接 `reader_->join(LT, PUBLIC)`，缓存 TD PUBLIC 所有帧
- `inspect_channel`（[cached.cpp:160-165](../src/yijinjing/cache/cached.cpp#L160-165)）对**所有**非自身的 Channel 事件 join (source, dest_id)，自动缓存所有跨进程 journal

### 阶段 2：ledger 注册（priority=15，第 3 个进程）

默认通道同上。

| Channel (source → dest) | 由谁触发 | 代码路径 | 备注 |
|---|---|---|---|
| **L → C** (ledger → cached private) | apprentice 基类非 cached 进程在 `request_cached_reader_writer` 里 `request_write_to(now(), U_C)` | [apprentice.cpp:96](../src/yijinjing/practice/apprentice.cpp#L96) | ledger 往 cached 写请求；master 通过 `RequestWriteTo` → `on_request_write_to` → `register_channel` 广播 `Channel{U_L, U_C}` |
| **C → L** (cached → ledger private) | 同上 `request_read_from(now(), U_C, now())` | [apprentice.cpp:97](../src/yijinjing/practice/apprentice.cpp#L97) | ledger 读 cached；master 广播 `Channel{U_C, U_L}` |
| **ledger 读 (CM_L → U_L)** | `reader_->join(CM_L, U_L, begin_time)` | [apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192) | 基类必建关系 |

**Ledger 作为 AutoClient + Bookkeeper 会动态对后续所有 Register（md/td/strategy）发起连接。**

### 阶段 3：md_sim 注册（priority=20，第 4 个进程，category=MD）

默认 3 条 (D,PUBLIC), (D,SYNC), (D,CM_D)。Ledger (AutoClient) 和 Strategy (PassiveClient) 在收到 `Register{MD, sim/sim}` 时触发 [client.cpp:156-159](../src/wingchun/broker/client.cpp#L156-159)：

```cpp
if (MD and should_connect_md) {
  app_.request_write_to(now(), U_D);          // 申请写 MD 的私有通道
  app_.request_read_from_public(now(), U_D);   // 申请读 MD PUBLIC
}
```

| Channel (source → dest) | master side handler | 产生新 Channel? | 备注 |
|---|---|---|---|
| **L → D** (ledger → md private) | `request_write_to` → [master.cpp:279-297](../src/yijinjing/practice/master.cpp#L279-297) | ✅ `Channel{U_L, U_D}` | ledger 向 MD 写 InstrumentKey（订阅请求） |
| **S → D** (strategy → md private) | 同上（strategy PassiveClient connect） | ✅ `Channel{U_S, U_D}` | strategy 在 `RuntimeContext::subscribe` 里 `broker_client_.connect` 触发 |
| **D → L (PUBLIC=0)** | `request_read_from_public(ledger→D)` → [master.cpp:317-320](../src/yijinjing/practice/master.cpp#L317-320) 内部 `require_write_to(U_D, PUBLIC)` | ❌ 复用已注册的 (D,0) writer | 这类"PUBLIC/SYNC 读请求"不产生新 Channel，因为所有 app 在 register_app 时已默认能写 PUBLIC/SYNC |
| **D → S (PUBLIC=0)** | 同上，由 strategy PassiveClient 触发 | ❌ 复用 | strategy 在 MD Ready 后 `request_read_from_public` |

**Cached 侧：** 对 MD 的 Register 事件，cached 在 `register_trigger_listen_public` 里 **只对 category==TD 分支 join PUBLIC**，**MD 的 PUBLIC 不主动 join**。但 cached 有 `inspect_channel`，会对所有 Channel 事件中非自身相关的 (source,dest_id) pair join。

### 阶段 4：td_sim 注册（priority=20，第 5 个进程，category=TD）

默认 3 条 (T,PUBLIC), (T,SYNC), (T,CM_T)。AutoClient / PassiveClient 对 TD 连 4 条（[client.cpp:161-167](../src/wingchun/broker/client.cpp#L161-167)）：

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
| **L → T** (ledger → td 私有) | ✅ `Channel{U_L, U_T}` | ledger 给 TD 写 ResetBookRequest 等；Ledger::on_start 的 `refresh_books()` 还会再调用一次 `request_write_to(U_T)`（[ledger.cpp:72-79](../src/wingchun/service/ledger.cpp#L72-79)），幂等 |
| **S → T** (strategy → td 私有) | ✅ `Channel{U_S, U_T}` | strategy 下 OrderInput 的通道；由 strategy `ensure_connect` 中 `broker_client_.connect(TD Register)` 触发的 `request_write_to` 建立 |
| **T → L (dest=U_L)**（TD → ledger 的私有订单回报） | ✅ `Channel{U_T, U_L}` | `request_read_from(ledger→T)` → master `on_request_read_from` 反向建 `require_write_to(U_T, U_L)` |
| **T → S (dest=U_S)**（TD → strategy 的私有订单回报） | ✅ `Channel{U_T, U_S}` | 同上，由 strategy 的 `request_read_from` 创建 |
| **T → L (PUBLIC=0)** | ❌ 复用 (T,0) writer；ledger `reader_->join(LT, 0)` | TD PUBLIC→ledger 可读 BrokerStateUpdate |
| **T → S (PUBLIC=0)** | ❌ 复用；strategy `reader_->join(LT, 0)` | TD PUBLIC→strategy 可读 BrokerStateUpdate |
| **T → L (SYNC=1)** | ❌ 复用 (T,1) writer；ledger `reader_->join(LT, 1)` | TD SYNC→ledger 读 Asset/Position 快照 |
| **T → S (SYNC=1)** | ❌ 复用；strategy `reader_->join(LT, 1)` | strategy 的 PassiveClient 也会读 TD SYNC |

**Cached 对 TD Register 的专门处理：** cached 对 TD 的 Register 事件直接 `reader_->join(LT, PUBLIC)` 缓存 TD PUBLIC 所有帧。对上面产生的 Channel（L→T、S→T、T→L、T→S）cached 也会通过 `inspect_channel` 全部 join。

### 阶段 5：strategy 注册（priority=30，第 6 个进程）

默认 3 条 (S,PUBLIC), (S,SYNC), (S,CM_S)。Ledger 的 AutoClient 对 category=STRATEGY 连 3 条（[client.cpp:168-173](../src/wingchun/broker/client.cpp#L168-173)）：

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

**Strategy 对已注册的 D / T 的 PassiveClient 连接：** 在 Runner::prepare / RuntimeContext::ensure_connect 阶段会检查 `app_.get_registry()` 和 `app_.get_bands()`，对其中的 MD/TD 调用 connect。由于 D/T 先注册，这一步会立即执行上面阶段 3/4 的所有 request_write/request_read 动作。

---

## 五、最终 Journal 文件清单（按 source location 分组）

按 `find_page_size` 规则（[page.h:84-94](../include/kungfu/yijinjing/journal/page.h#L84-94)），在 locator 的 JOURNAL 目录下会有这些 location 子目录，各含若干 journal page 文件。

### 5A. SYSTEM/master/{mode}（master home location）
| dest_id | 文件名（首 page） | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB | master（全局广播 Location/Register/Channel/Band/Deregister/TradingDay 等） | 所有进程（基类 `reader_->join(master_home, PUBLIC)`） |
| 1 (SYNC) | `00000001.1.journal` | 1MB | master（register_app 默认开，实际空） | master 默认 join |
| `U_C` | `{U_C:08x}.1.journal` | 1MB | master（给 cached 的备用通道，默认开但空） | master 默认 join |

### 5B. SYSTEM/service/cached/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB | cached（实际很少写 PUBLIC） | master 默认 join + 其他有 join PUBLIC 的进程 |
| 1 (SYNC) | `00000001.1.journal` | 1MB | cached（空） | master 默认 join |
| `UCM_C` = Cached 进程的 master_cmd pseudo uid | `{UCM_C:08x}.1.journal` | 1MB | master（SessionStart / TimeReset / RequestCachedDone） | cached（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_L` | `{U_L:08x}.1.journal` | 1MB（SYSTEM 类默认） | cached（ledger 读 cached） | ledger（`request_read_from(now(), U_C)` → `reader_->join(cached_home, U_L)`） |

### 5C. SYSTEM/service/ledger/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB | ledger | master 默认 join |
| 1 (SYNC) | `00000001.1.journal` | 1MB | ledger（空） | master 默认 join |
| `UCM_L` | `{UCM_L:08x}.1.journal` | 1MB | master（命令通道） | ledger（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_C` | `{U_C:08x}.1.journal` | 1MB | ledger（`request_write_to(now(), U_C)`） | cached（RequestReadFrom 反向） |
| `U_D` | `{U_D:08x}.1.journal` | 1MB（ledger SYSTEM 类） | ledger（AutoClient `request_write_to`） | MD（ledger → MD 的 InstrumentKey 订阅请求） |
| `U_T` | `{U_T:08x}.1.journal` | 1MB | ledger（写 ResetBookRequest、订阅） | TD |
| `U_S` | `{U_S:08x}.1.journal` | 1MB | ledger（写对账、book reset） | strategy |

### 5D. MD/sim/sim/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | **128MB**（MD dest!=1） | MD（Quote / Trade / InstrumentKey / 自定义行情） | master 默认 join / ledger `reader_->join(MD,0)` / strategy `reader_->join(MD,0)` / cached 通过 inspect_channel |
| 1 (SYNC) | `00000001.1.journal` | 1MB（MD dest==1） | MD（空，注册时默认开，但 MD 不发同步快照——没有持仓概念） | master 默认 join |
| `UCM_D` | `{UCM_D:08x}.1.journal` | 1MB | master（命令通道） | MD（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_L` | `{U_L:08x}.1.journal` | 1MB | MD（给 ledger 的私回报，一般没有） | ledger（收到 `RequestWriteTo` 后 writer emplace，master join） |
| `U_S` | `{U_S:08x}.1.journal` | 1MB | MD（给 strategy 的私回报，同上） | strategy（同上） |

### 5E. TD/sim/sim/{mode}
| dest_id | 文件名 | 大小 | 谁写 | 谁读 |
|---|---|---|---|---|
| 0 (PUBLIC) | `00000000.1.journal` | 1MB（TD dest==0） | TD（BrokerStateUpdate / Order 广播 / Trade 广播） | master 默认 join / ledger join / strategy join / cached 显式 join（[cached.cpp:192](../src/yijinjing/cache/cached.cpp#L192)） |
| 1 (SYNC) | `00000001.1.journal` | **16MB**（TD dest!=0） | TD（Asset 快照 / AssetMargin 快照 / Position 快照 / PositionEnd 快照 —— Trader 启动 `enable_asset_sync` 阶段写，之后切回 PUBLIC 增量）见 [trader.cpp:98-108](../src/wingchun/broker/trader.cpp#L98-108) / bookkeeper 的 `fork<*>::SYNC` [bookkeeper.cpp:73-76](../src/wingchun/book/bookkeeper.cpp#L73-76) | master 默认 join / ledger join SYNC / strategy join SYNC |
| `UCM_T` | `{UCM_T:08x}.1.journal` | 1MB | master（命令通道） | TD（[apprentice.cpp:192](../src/yijinjing/practice/apprentice.cpp#L192)） |
| `U_L` | `{U_L:08x}.1.journal` | 16MB（TD dest!=0） | TD（给 ledger 的私订单回报） | ledger（`request_read_from(ledger→TD)` → master 建反向 write_to(TD, U_L)） |
| `U_S` | `{U_S:08x}.1.journal` | 16MB | TD（给 strategy 的私订单回报、成交、持仓） | strategy（同上） |

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
| 默认 0 / 1（register_app 会默认开） | `00000000.1.journal` / `00000001.1.journal` | master（register_app `require_write_to(CMx, 0/1)`，但 master_cmd 是伪 location，只有 dest=Ux 这一条有数据） | |

**注意**：master_cmd_{hex} 是**单向**伪 location：master 写 dest=对应 app_uid，不写 PUBLIC/SYNC；但 `register_app` 中 `require_write_to(CMx, PUBLIC/SYNC/master_cmd_uid)` 会发起 3 条，所以也存在 0/1 页文件，只是为空。

---

## 六、最终 Channel 注册（{source, dest} 对）汇总表

所有 `Channel{source, dest}` 由 master 广播到 master PUBLIC（[master.cpp:296](../src/yijinjing/practice/master.cpp#L296)），可被所有 Channel::tag 订阅者 / cached / ledger 的 inspect_channel 使用。

### A. register_app 每个 app 默认开的 3 条 writer 不算 Channel
3A 的 (X,PUBLIC), (X,SYNC), (X,CMX) 是通过 `require_write_to` 完成的，但它不发 `Channel` 事件（只有 `on_request_write_to`/`on_request_read_from` 发）。所以它们不计入 Channel map。

### B. App 之间显式建的 Channel

按"谁发起 request"汇总，每个 request 对应一个 Channel：

| # | source_id | dest_id | 发起方 | 触发 API | 调用点 |
|---|-----------|---------|--------|----------|--------|
| 1 | U_L (ledger) | U_C (cached) | ledger | `request_write_to(now(), U_C)` | [apprentice.cpp:96](../src/yijinjing/practice/apprentice.cpp#L96) |
| 2 | U_C (cached) | U_L (ledger) | ledger | `request_read_from(now(), U_C, now())` → master 反向建 `require_write_to(U_C, U_L)` | [apprentice.cpp:97](../src/yijinjing/practice/apprentice.cpp#L97) → [master.cpp:308](../src/yijinjing/practice/master.cpp#L308) |
| 3 | U_L | U_D (md) | ledger AutoClient MD connect | `request_write_to(U_D)` | [client.cpp:157](../src/wingchun/broker/client.cpp#L157) → [master.cpp:279-297](../src/yijinjing/practice/master.cpp#L279-297) |
| 4 | U_S (strategy) | U_D | strategy PassiveClient MD connect | `request_write_to(U_D)` | [client.cpp:157](../src/wingchun/broker/client.cpp#L157)（strategy 内部调用 connect(MD Register)） |
| 5 | U_L | U_T (td) | ledger AutoClient TD connect | `request_write_to(U_T)` | [client.cpp:162](../src/wingchun/broker/client.cpp#L162) → [master.cpp:279-297](../src/yijinjing/practice/master.cpp#L279-297) |
| 6 | U_S | U_T | strategy PassiveClient TD connect / `ensure_connect` | `request_write_to(U_T)` | [client.cpp:162](../src/wingchun/broker/client.cpp#L162) / [runtime.cpp:355-370](../src/wingchun/strategy/runtime.cpp#L355-370) |
| 7 | U_T | U_L | ledger AutoClient TD connect | `request_read_from(U_T)` → master `require_write_to(U_T, U_L)` | [client.cpp:163](../src/wingchun/broker/client.cpp#L163) → [master.cpp:308](../src/yijinjing/practice/master.cpp#L308) |
| 8 | U_T | U_S | strategy PassiveClient TD connect | `request_read_from(U_T)` → master `require_write_to(U_T, U_S)` | [client.cpp:163](../src/wingchun/broker/client.cpp#L163) |
| 9 | U_L | U_S (strategy) | ledger AutoClient STRATEGY connect | `request_write_to(U_S)` | [client.cpp:169](../src/wingchun/broker/client.cpp#L169) → [master.cpp:279-297](../src/yijinjing/practice/master.cpp#L279-297) |
| 10 | U_S | U_L | ledger AutoClient STRATEGY connect | `request_read_from(U_S)` → master `require_write_to(U_S, U_L)` | [client.cpp:170](../src/wingchun/broker/client.cpp#L170) |

**合计：10 条显式 Channel**（都会有 `Channel` 事件广播）。加上每个 app 默认的 PUBLIC/SYNC/CMD 三条（不计入 Channel map，但有独立 journal 文件），系统运行时总共 **28 条左右独立的 journal 文件簇**（按 (source, dest_id) 对），每簇包含若干连续 page 文件（写满自动翻页）。

> **注意**：[strategy101.h](../apps/strategies/strategy101.h) 的 `RuntimeContext::subscribe("sim", {"600000"}, "SSE")` 会对 MD 再触发一次 `ensure_connect` → `send_instrument_keys`。但这只是在已建立的 Channel (U_S→U_D) 上写 `InstrumentKey` 帧，不会新建 Channel，也不会新建 journal 文件；MD Ready 后，strategy PassiveClient 的 `renew()` 会用已打开的 `writer(U_D)` 重发 InstrumentKey，仍然复用已建通道。

---

## 七、核心机制总结

1. **底层介质**: 所有通信均通过内存映射文件 (mmap) 实现零拷贝 IPC。没有 socket、没有 RPC。
2. **路由机制**: `dest_id` 作为"端口号"，区分了 PUBLIC (0)、SYNC (1)、和私有通道 (App UID)。`find_page_size` 根据 category 和 dest_id 自动选择合适的 page 大小。
3. **自动连接**: 系统通过 `Register` 和 `Channel` 事件驱动，自动完成了所有必要的连接（订阅行情、提交订单、同步状态），无需手动配置。
4. **事件总线**: Master 的 PUBLIC journal 作为系统事件总线，所有进程 join 它来获取 Register、Location、Channel、Band 等元事件。
5. **缓存机制**: Cached 服务通过 `inspect_channel` 监听所有 Channel 事件，自动将所有跨进程的私有 journal 纳入缓存，实现了全量数据的持久化和溯源。
