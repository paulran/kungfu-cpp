# kf_master 启动流程分析

## 概述

`kf_master` 是 Kungfu 框架的核心服务进程，负责管理整个系统中所有应用程序的注册、通信通道建立、时间同步等关键功能。本文档详细分析其启动过程中执行的所有操作。

---

## 启动流程总览

```
main()
  │
  ├─→ 解析命令行参数
  │
  ├─→ 创建 locator 和 home location
  │
  ├─→ master_app 构造函数
  │     │
  │     ├─→ hero 构造函数
  │     │     │
  │     │     ├─→ io_device 构造函数
  │     │     │     │
  │     │     │     └─→ 日志初始化 + SQLite初始化
  │     │     │
  │     │     ├─→ io_device_master 构造函数
  │     │     │     │
  │     │     │     └─→ 创建 nanomsg Publisher/Observer (PUB/PULL)
  │     │     │
  │     │     ├─→ 设置 OS 信号处理
  │     │     │
  │     │     ├─→ 注册系统预定义 location
  │     │     │
  │     │     └─→ 创建 reader
  │     │
  │     ├─→ profile_.setup() - 加载配置
  │     │
  │     ├─→ 从 profile 加载预配置的 Location 和 Config
  │     │
  │     ├─→ session_builder_.open_session() - 创建 master session
  │     │
  │     ├─→ 创建 PUBLIC writer
  │     │
  │     └─→ 写入 SessionStart 标记
  │
  ├─→ app.run()
  │     │
  │     ├─→ io_device_->setup() - 初始化 nanomsg socket
  │     │
  │     ├─→ 创建事件流 Observable
  │     │
  │     ├─→ react() - 注册所有事件处理器
  │     │
  │     └─→ 事件循环启动
  │           │
  │           ├─→ drain() - 读取并分发事件
  │           ├─→ on_active() - 每帧活跃处理
  │           └─→ on_frame() - 定时器任务处理
  │
  └─→ on_exit() - 退出清理
```

---

## 1. 命令行参数解析

**文件**: [master.cpp](../apps/master.cpp#L33-L51)

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--mode` | 运行模式 | `live` |
| `--low-latency` | 低延迟模式 | `false` |

模式映射：
- `live` → `mode::LIVE` (实盘模式)
- `sim` → `mode::DATA` (模拟模式)
- `replay` → `mode::REPLAY` (回放模式)

---

## 2. 核心对象创建

### 2.1 locator 创建

```cpp
auto loc = std::make_shared<locator>(m);
auto home = location::make_shared(m, category::SYSTEM, "master", "master", loc);
```

`locator` 负责管理系统中所有 location 的路径布局和 UID 生成。`home` 是 master 进程的核心身份标识。

### 2.2 master_app 构造函数

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L22-L37)

#### 2.2.1 hero 构造函数调用

**文件**: [hero.cpp](../src/yijinjing/practice/hero.cpp#L30-L45)

```cpp
master::master(location_ptr home, bool low_latency)
    : hero(std::make_shared<io_device_master>(home, low_latency)), ...
```

**hero 构造函数执行步骤**:

| 步骤 | 操作 | 说明 |
|------|------|------|
| 1 | 设置时间范围 | `begin_time_ = now()`, `end_time_ = INT64_MAX` |
| 2 | 创建系统位置 | master_home, master_cmd, cached_home, ledger_home |
| 3 | OS信号处理 | `os::handle_os_signals(this)` |
| 4 | 注册系统位置 | 将所有系统位置添加到 `locations_` 映射表 |
| 5 | 创建 reader | `reader_ = io_device_->open_reader_to_subscribe()` |

#### 2.2.2 io_device 基类构造函数

**文件**: [io.cpp](../src/yijinjing/io/io.cpp#L139-L148)

```cpp
io_device::io_device(data::location_ptr home, const bool low_latency, const bool lazy)
    : home_(std::move(home)), low_latency_(low_latency), lazy_(lazy) {
  if (spdlog::default_logger()->name().empty()) {
    yijinjing::log::setup_log(home_, home_->name);
  }
  ensure_sqlite_initilize();
  live_home_ = location::make_shared(mode::LIVE, home_->category, home_->group, home_->name, home_->locator);
  url_factory_ = std::make_shared<ipc_url_factory>();
}
```

**关键初始化操作**:

| 操作 | 说明 |
|------|------|
| 日志初始化 | 使用 home location 配置 spdlog |
| SQLite初始化 | `ensure_sqlite_initilize()` |
| live_home 创建 | 生成 LIVE 模式下的实际 home |
| URL工厂创建 | 用于生成 nanomsg IPC 路径 |

---

## 3. Profile 配置加载

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L25-L31)

```cpp
profile_.setup();
for (const auto &app_location : profile_.get_all(Location{})) {
  add_location(start_time_, location::make_shared(app_location, get_locator()));
}
for (const auto &config : profile_.get_all(Config{})) {
  try_add_location(start_time_, location::make_shared(config, get_locator()));
}
```

### 3.1 profile::setup() 详细实现

**文件**: [profile.cpp](../src/yijinjing/practice/profile.cpp#L24-L28)

```cpp
void profile::setup() {
  auto storage = get_storage();
  storage->pragma.journal_mode(sqlite_orm::journal_mode::WAL);
  storage->sync_schema();
}
```

**执行步骤**:

| 步骤 | 操作 | 说明 |
|------|------|------|
| 1 | `get_storage()` | 获取 SQLite 存储实例（延迟初始化，线程安全） |
| 2 | 设置 WAL 模式 | `storage->pragma.journal_mode(WAL)` - 开启 Write-Ahead Logging |
| 3 | `sync_schema()` | 同步数据库 schema，自动创建缺失的表 |

**数据库文件路径**: 由 `default_db_file()` 生成，路径为 `${LOCATOR_ROOT}/SYSTEM/etc/kungfu/sqlite/config`

**功能说明**:
- `profile_` 是一个基于 SQLite 的配置存储，使用 sqlite_orm 库
- `setup()` 确保数据库以 WAL 模式运行（支持并发读写）
- 从数据库加载所有预定义的 `Location` 和 `Config` 记录
- 将这些配置转换为 location 对象并注册到 `locations_` 映射表

---

## 4. Session 初始化

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L33-L37)

```cpp
auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
session_builder_.open_session(master_home_location_, start_time_);
writers_.emplace(location::PUBLIC, io_device->open_writer(location::PUBLIC));
get_writer(location::PUBLIC)->mark(start_time_, SessionStart::tag);
```

### 4.1 session_builder 详细说明

**文件**: [session.h](../include/kungfu/yijinjing/index/session.h#L38-L56)、[session.cpp](../src/yijinjing/index/session.cpp#L57-L114)

`session_builder` 继承自 `session_finder`，负责管理系统中所有应用的 session 生命周期：

| 方法 | 功能 |
|------|------|
| `open_session()` | 创建新的 session，记录开始时间 |
| `close_session()` | 关闭指定 session，记录结束时间 |
| `close_all_sessions()` | 关闭所有活跃 session |
| `update_session()` | 更新 session 最后活跃时间（keep-alive） |
| `find_last_active_time()` | 查询应用上次活跃时间 |
| `rebuild_index_db()` | 重建 session 索引数据库 |

#### 4.1.1 open_session() 实现

```cpp
Session &session_builder::open_session(const location_ptr &source_location, int64_t time) {
  auto pair = live_sessions_.try_emplace(source_location->uid);
  auto &session = pair.first->second;
  if (pair.second) {
    session.location_uid = source_location->uid;
    session.category = source_location->category;
    session.group = source_location->group;
    session.name = source_location->name;
    session.mode = source_location->mode;
  }
  session.begin_time = time;
  session.end_time = 0;
  session.update_time = time;
  session_storage_->replace(session);
  return session;
}
```

**执行逻辑**:
1. 尝试在 `live_sessions_` 中插入新 session（如果已存在则获取现有引用）
2. 如果是新 session，初始化 location 信息（uid、category、group、name、mode）
3. 设置 `begin_time` 和 `update_time` 为当前时间，`end_time` 为 0（表示活跃）
4. 将 session 持久化到 SQLite 数据库

#### 4.1.2 update_session() 实现

```cpp
void session_builder::update_session(const frame_ptr &frame) {
  if (live_sessions_.find(frame->source()) == live_sessions_.end()) {
    return;
  }
  Session &session = live_sessions_.at(frame->source());
  session.update_time = frame->gen_time();
  session.frame_count++;
  session.data_size += frame->frame_length();
}
```

**执行逻辑**:
1. 检查 session 是否存在于 `live_sessions_` 中
2. 更新 `update_time` 为帧的生成时间（keep-alive）
3. 增加 `frame_count` 计数器
4. 累加 `data_size`（记录传输的数据量）

**核心数据结构**:
- `live_sessions_`: `std::unordered_map<uint32_t, Session>` - 存储所有活跃 session
- `session_storage_`: SQLite 存储，持久化所有 session 记录

**数据库文件路径**: `${LOCATOR_ROOT}/SYSTEM/journal/index/sqlite/index`

**关键操作**:

| 操作 | 说明 |
|------|------|
| `session_builder_.open_session()` | 创建 master 的 journal session，记录到 SQLite |
| `open_writer(location::PUBLIC)` | 创建公共广播 writer，用于向所有应用广播消息 |
| `mark(SessionStart::tag)` | 在 PUBLIC channel 写入会话开始标记，通知所有监听者 |

---

## 4.2 Journal 共享内存机制

`kf_master` 使用 **基于文件映射的共享内存**（mmap）来实现进程间高效通信。这是 Kungfu 框架的核心技术之一。

### 4.2.1 共享内存创建时机

共享内存在以下时机被创建：

| 时机 | 触发条件 | 创建位置 |
|------|----------|----------|
| writer 创建 | `open_writer()` 调用时 | 对应 location 的 journal 目录 |
| reader 订阅 | `reader_->join()` 调用时 | 对应 location 的 journal 目录 |
| 应用注册 | `register_app()` 过程中 | 应用的 journal 目录 |

### 4.2.2 mmap 实现原理

**文件**: [mmap.cpp](../src/yijinjing/util/mmap.cpp#L27-L97)

```cpp
uintptr_t load_mmap_buffer(const std::string &path, size_t size, bool is_writing, bool lazy) {
  bool master = is_writing || !lazy;
  int fd = open(path.c_str(), (master ? O_RDWR : O_RDONLY) | O_CREAT, (mode_t)0600);
  if (master) {
    // 扩展文件大小到指定 size
    if (lseek(fd, size - 1, SEEK_SET) == -1) { ... }
    if (write(fd, "", 1) == -1) { ... }
  }
  // 创建 MAP_SHARED 共享内存映射
  void *buffer = mmap(0, size, master ? (PROT_READ | PROT_WRITE) : PROT_READ, MAP_SHARED, fd, 0);
  if (!lazy && madvise(buffer, size, MADV_RANDOM) != 0 && mlock(buffer, size) != 0) {
    // 非懒加载模式下锁定内存到物理内存
    ...
  }
  close(fd);
  return reinterpret_cast<uintptr_t>(buffer);
}
```

**关键技术点**:

| 技术 | 说明 |
|------|------|
| `MAP_SHARED` | 多个进程共享同一块内存区域，修改对所有进程可见 |
| `O_CREAT` | 如果文件不存在则创建 |
| `lseek` + `write` | 将文件扩展到指定大小 |
| `mlock` | 非懒加载模式下锁定内存，防止被交换到磁盘 |
| `MADV_RANDOM` | 告知内核采用随机访问模式 |

### 4.2.3 Journal Page 结构

**文件**: [page.cpp](../src/yijinjing/journal/page.cpp#L27-L65)

```cpp
page_ptr page::load(const data::location_ptr &location, uint32_t dest_id, uint32_t page_id, 
                    bool is_writing, bool lazy) {
  uint32_t page_size = find_page_size(location, dest_id);
  std::string path = get_page_path(location, dest_id, page_id);
  uintptr_t address = os::load_mmap_buffer(path, page_size, is_writing, lazy);
  // ... 初始化 page header
}
```

**Page 文件路径**: `${LOCATOR_ROOT}/${LOCATION_UNAME}/journal/${DEST_ID}.${PAGE_ID}`

**Page Header 结构**:

| 字段 | 类型 | 说明 |
|------|------|------|
| `version` | uint32 | journal 版本号 |
| `page_header_length` | uint32 | header 长度 |
| `page_size` | uint32 | page 总大小 |
| `frame_header_length` | uint32 | frame header 长度 |
| `last_frame_position` | uint64 | 最后一个 frame 的位置 |

### 4.2.4 共享内存生命周期

```
writer 创建 / reader join
         │
         ▼
open() 创建/打开 journal 文件
         │
         ▼
lseek() + write() 扩展文件大小
         │
         ▼
mmap() 创建共享内存映射 (MAP_SHARED)
         │
         ▼
[非懒加载模式] mlock() 锁定内存
         │
         ▼
写入/读取 journal frame
         │
         ▼
page 析构 → munmap() 解除映射
         │
         ▼
[非懒加载模式] munlock() 解锁内存
```

### 4.2.5 Master 启动时创建的共享内存

在 `kf_master` 启动过程中，以下共享内存会被创建：

| 共享内存 | 创建时机 | 用途 |
|----------|----------|------|
| PUBLIC journal | `open_writer(location::PUBLIC)` | 公共广播通道，向所有应用广播消息 |
| master_cmd journal | `register_app()` 时 | master 向特定应用发送命令 |
| 应用 journal | `register_app()` 时 | 应用之间的通信通道 |

### 4.2.6 设计优势

| 优势 | 说明 |
|------|------|
| **零拷贝** | 进程间直接读写同一块内存，无需数据拷贝 |
| **持久化** | 内存映射到文件，数据自动持久化到磁盘 |
| **高吞吐** | 避免 socket 通信的开销，适合高频数据传输 |
| **跨进程** | 多个进程可以同时读写同一块内存 |
| **内存锁定** | 非懒加载模式下锁定内存，保证低延迟 |

---

## 5. 运行阶段 - run() 方法

**文件**: [hero.cpp](../src/yijinjing/practice/hero.cpp#L68-L76)

```cpp
void hero::run() {
  SPDLOG_INFO("[{:08x}] {} running", get_home_uid(), get_home_uname());
  SPDLOG_TRACE("from {} until {}", time::strftime(begin_time_), time::strftime(end_time_));
  setup();
  continual_ = true;
  events_.connect(cs_);
  on_exit();
  SPDLOG_INFO("[{:08x}] {} done", get_home_uid(), get_home_uname());
}
```

### 5.1 setup() 方法

**文件**: [hero.cpp](../src/yijinjing/practice/hero.cpp#L56-L61)

```cpp
void hero::setup() {
  io_device_->setup();
  events_ = observable<>::create<event_ptr>([this](auto &s) { delegate_produce(this, s); }) | holdon();
  react();
  live_ = true;
}
```

**setup() 执行步骤**:

| 步骤 | 操作 | 说明 |
|------|------|------|
| 1 | `io_device_->setup()` | 初始化 publisher 和 observer |
| 2 | 创建事件流 | 使用 Rx 模式创建可观察事件流 |
| 3 | `react()` | 注册所有事件处理器（见下文） |
| 4 | 设置 live_ 标志 | 标记为活跃状态 |

#### 5.1.1 io_device::setup()

**文件**: [io.h](../include/kungfu/yijinjing/io.h#L24-L27)

```cpp
void setup() override {
  publisher_->setup();
  observer_->setup();
}
```

对于 master 模式：
- `nanomsg_publisher_master::setup()` - 空操作（已在构造时 bind）
- `nanomsg_observer_master::setup()` - 设置接收超时时间

#### 5.1.2 react() 方法 - 事件处理器注册

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L163-L188)

| 消息类型 | 处理器 | 功能说明 |
|----------|--------|----------|
| `RequestWriteTo::tag` | `on_request_write_to()` | 处理写通道请求 |
| `RequestWriteToBand::tag` | `on_request_write_to_band()` | 处理 Band 写请求 |
| `RequestReadFrom::tag` | `on_request_read_from()` + `check_cached_ready_to_read()` | 处理读通道请求 |
| `RequestReadFromPublic::tag` | `on_request_read_from_public()` | 处理公共读请求 |
| `RequestReadFromSync::tag` | `on_request_read_from_sync()` | 处理同步读请求 |
| `RequestStop::tag` | `signal_stop()` | 停止 master（仅限 SYSTEM/master 来源） |
| `ChannelRequest::tag` | `on_channel_request()` | 处理通道请求 |
| `TimeRequest::tag` | `on_time_request()` | 处理定时任务请求 |
| `Location::tag` | `on_new_location()` | 处理新位置通知 |
| `Register::tag` | `register_app()` | 处理应用注册 |
| `RequestCachedDone::tag` | `on_request_cached_done()` | 处理缓存完成通知 |
| `Ping::tag` | `pong()` | 处理心跳请求 |
| `journal::frame` | `feed()` | 处理日志帧 |

### 5.2 事件循环

**文件**: [hero.cpp](../src/yijinjing/practice/hero.cpp#L331-L344)

```cpp
void hero::produce(const rx::subscriber<event_ptr> &sb) {
  try {
    do {
      live_ = drain(sb) && live_;
      on_active();
    } while (continual_ and live_);
  } catch (...) {
    live_ = false;
    sb.on_error(std::current_exception());
  }
  if (not live_) {
    sb.on_completed();
  }
}
```

**事件循环核心逻辑**:

#### 5.2.1 drain() 方法

**文件**: [hero.cpp](../src/yijinjing/practice/hero.cpp#L346-L375)

| 操作 | 说明 |
|------|------|
| `observer_->wait()` | 等待 nanomsg 通知 |
| 处理 notice | 如果有 JSON 消息则推送到事件流 |
| `reader_->data_available()` | 检查 journal 是否有新数据 |
| 推送帧 | 将 journal 帧推送到事件流 |
| `on_frame()` | 每帧回调 |

#### 5.2.2 on_active() 方法

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L190-L197)

```cpp
void master::on_active() {
  auto now = time::now_in_nano();
  if (last_check_ + time_unit::NANOSECONDS_PER_SECOND < now) {
    on_interval_check(now);
    last_check_ = now;
  }
  on_frame();
}
```

**功能**:
- 每秒调用一次 `on_interval_check()`（用户可重写）
- 调用 `on_frame()` 处理定时器任务

#### 5.2.3 on_frame() 方法

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L199)

```cpp
void master::on_frame() { handle_timer_tasks(); }
```

**handle_timer_tasks()**: 遍历所有定时器任务，检查是否到达触发时间，触发后写入 `Time::tag` 标记。

#### 5.2.4 feed() 方法 - Session Keep-Alive

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L243-L251)

```cpp
void master::feed(const event_ptr &event) {
  handle_timer_tasks();
  if (registry_.find(event->source()) == registry_.end()) {
    return;
  }
  session_builder_.update_session(std::dynamic_pointer_cast<journal::frame>(event));
}
```

**功能**:
- 接收所有 journal frame 事件
- 检查事件来源是否已注册
- 更新 session 的最后活跃时间（keep-alive 机制）
- 确保 session_builder 能够追踪应用的活跃状态

#### 5.2.5 on_request_cached_done() 方法 - 客户端启动同步

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L327-L341)

```cpp
void master::on_request_cached_done(const event_ptr &event) {
  auto request_cached_done_data = event->data<RequestCachedDone>();
  auto app_uid = request_cached_done_data.dest_id;

  if (has_writer(app_uid)) {
    auto app_cmd_writer = get_writer(app_uid);
    app_cmd_writer->mark(now(), RequestStart::tag);
    write_locations(event->gen_time(), app_cmd_writer);
    write_registries(event->gen_time(), app_cmd_writer);
    write_channels(event->gen_time(), app_cmd_writer);
    write_bands(event->gen_time(), app_cmd_writer);
  }
}
```

**功能**:
- 当客户端完成缓存数据加载后，发送 `RequestCachedDone` 通知 master
- master 收到通知后，向客户端发送完整的系统状态：
  - `RequestStart` 标记：通知客户端可以开始请求
  - `write_locations()`：发送所有已知位置信息
  - `write_registries()`：发送所有已注册应用信息
  - `write_channels()`：发送所有通信通道信息
  - `write_bands()`：发送所有 Band 信息

**设计意图**: 这是客户端启动同步流程的关键环节。客户端在启动时需要先加载缓存数据，完成后通知 master，master 再发送最新的系统状态，确保客户端拥有完整的系统视图。

---

## 5.3 应用注册流程 - register_app()

`register_app()` 是 master 最重要的运行时职责，负责处理客户端应用的注册请求。

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L90-L143)

### 注册流程详解

```
客户端发送 Register 事件
         │
         ▼
1. 解析 Register 数据
         │
         ▼
2. 检查应用是否已注册 (is_location_live)
         │
         ▼
3. 创建 master_cmd_location (系统命令通道)
         │
         ▼
4. 创建 app_cmd_writer (应用专属 writer)
         │
         ▼
5. 注册应用位置和命令位置 (try_add_location)
         │
         ▼
6. 查询上次活跃时间 (find_last_active_time)
         │
         ▼
7. 注册应用到 registry_ (register_location)
         │
         ▼
8. 保存 writer 到 writers_ 映射
         │
         ▼
9. reader 订阅应用数据 (join PUBLIC, SYNC, master_cmd)
         │
         ▼
10. 创建应用 session (open_session)
         │
         ▼
11. 写入 SessionStart 标记
         │
         ▼
12. 广播应用位置和注册信息到 PUBLIC
         │
         ▼
13. 设置写权限 (require_write_to PUBLIC, SYNC, master_cmd)
         │
         ▼
14. 发送时间重置和交易日信息
         │
         ▼
15. 发送所有已知位置信息
         │
         ▼
16. 发送所有已注册应用信息
         │
         ▼
17. 调用 on_register() 回调
```

### 关键代码步骤详解

#### 步骤 1-3: 位置验证与创建

```cpp
auto app_location = location::make_shared(register_data, home->locator);
if (is_location_live(app_location->uid)) {
  SPDLOG_ERROR("location {} has already been registered live", app_location->uname);
  return;
}
auto master_cmd_location = location::make_shared(mode::LIVE, category::SYSTEM, 
                                                 "master", uid_str, home->locator);
```

- 从 Register 数据创建应用 location 对象
- 检查应用是否已注册（防止重复注册）
- 创建 master_cmd_location，用于 master 向应用发送命令

#### 步骤 4-8: 通信通道建立

```cpp
auto app_cmd_writer = get_io_device()->open_writer_at(master_cmd_location, app_location->uid);
try_add_location(event->gen_time(), app_location);
try_add_location(event->gen_time(), master_cmd_location);
register_data.last_active_time = session_builder_.find_last_active_time(app_location);
register_location(event->gen_time(), register_data);
writers_.emplace(app_location->uid, app_cmd_writer);
```

- 创建应用专属 writer（写入 master_cmd_location）
- 注册应用位置和命令位置到 locations_
- 查询应用上次活跃时间（用于恢复）
- 将应用注册到 registry_
- 保存 writer 到 writers_ 映射

#### 步骤 9-11: Reader 订阅与 Session 创建

```cpp
reader_->join(app_location, location::PUBLIC, now);
reader_->join(app_location, location::SYNC, now);
reader_->join(app_location, master_cmd_location->uid, now);
session_builder_.open_session(app_location, event->gen_time());
app_cmd_writer->mark(event->gen_time(), SessionStart::tag);
```

- reader 订阅应用的 PUBLIC channel（公共广播）
- reader 订阅应用的 SYNC channel（同步数据）
- reader 订阅应用的 master_cmd channel（命令通道）
- 创建应用的 journal session
- 写入 SessionStart 标记

#### 步骤 12-16: 状态广播

```cpp
public_writer->write(event->gen_time(), *std::dynamic_pointer_cast<Location>(app_location));
public_writer->write(event->gen_time(), register_data);
require_write_to(event->gen_time(), app_location->uid, location::PUBLIC);
require_write_to(event->gen_time(), app_location->uid, location::SYNC);
require_write_to(event->gen_time(), app_location->uid, master_cmd_location->uid);
write_time_reset(event->gen_time(), app_cmd_writer);
write_trading_day(event->gen_time(), app_cmd_writer);
write_locations(event->gen_time(), app_cmd_writer);
write_registries(event->gen_time(), app_cmd_writer);
```

- 向 PUBLIC 广播应用位置和注册信息
- 设置应用写权限（PUBLIC, SYNC, master_cmd）
- 发送时间重置信息（同步时钟）
- 发送交易日信息
- 发送所有已知位置信息（让新应用了解系统拓扑）
- 发送所有已注册应用信息（让新应用了解其他应用）

#### 步骤 17: 回调通知

```cpp
on_register(event, register_data);
```

- 调用用户自定义的 `on_register()` 回调
- 可用于记录日志、初始化应用特定逻辑

---

## 6. 退出清理 - on_exit()

**文件**: [master.cpp](../src/yijinjing/practice/master.cpp#L39-L43)

```cpp
void master::on_exit() {
  notify_deregister_on_exit();
  mark_session_end_on_exit();
  notify_master_deregister_on_exit();
}
```

**退出流程**:

| 步骤 | 操作 | 说明 |
|------|------|------|
| 1 | `notify_deregister_on_exit()` | 向所有活跃应用发送 Deregister 消息 |
| 2 | `mark_session_end_on_exit()` | 在所有 writer 写入 SessionEnd 标记 |
| 3 | `notify_master_deregister_on_exit()` | 通知所有应用 master 已退出 |

---

## 关键数据结构

### 6.1 locations_

```cpp
std::unordered_map<uint32_t, yijinjing::data::location_ptr> locations_;
```

存储所有已知 location 的映射表，key 为 location UID。

### 6.2 registry_

```cpp
std::unordered_map<uint32_t, longfist::types::Register> registry_;
```

存储所有已注册的活跃应用信息。

### 6.3 channels_ / bands_

```cpp
std::unordered_map<uint64_t, longfist::types::Channel> channels_;
std::unordered_map<uint64_t, longfist::types::Band> bands_;
```

存储应用之间的通信通道，key 为 `source_id << 32 | dest_id`。

### 6.4 timer_tasks_

```cpp
std::unordered_map<uint32_t, std::unordered_map<int32_t, timer_task>> timer_tasks_;
```

存储各应用的定时器任务，第一层 key 为 app_uid，第二层 key 为 task_id。

---

## 启动时序图

```
main()
  │
  │ 1. 解析参数
  │
  │ 2. 创建 locator & home
  │
  ▼
master_app 构造
  │
  │ 3. hero 构造
  │     ├─→ io_device 构造
  │     │     ├─→ 日志初始化
  │     │     ├─→ SQLite初始化
  │     │     └─→ URL工厂创建
  │     │
  │     ├─→ io_device_master 构造
  │     │     ├─→ nanomsg_publisher_master (PUB bind)
  │     │     └─→ nanomsg_observer_master (PULL bind)
  │     │
  │     ├─→ OS信号处理注册
  │     │
  │     ├─→ 系统位置注册
  │     │     └─→ master_home, master_cmd, cached, ledger
  │     │
  │     └─→ reader 创建
  │
  │ 4. profile_.setup() - 加载配置
  │
  │ 5. 从 profile 加载 Location/Config
  │
  │ 6. session_builder_.open_session()
  │
  │ 7. 创建 PUBLIC writer
  │
  │ 8. 写入 SessionStart 标记
  │
  ▼
run()
  │
  │ 9. io_device_->setup()
  │     └─→ 设置 nanomsg socket 参数
  │
  │ 10. 创建事件流 Observable
  │
  │ 11. react() - 注册事件处理器
  │
  │ 12. 设置 live_ = true
  │
  ▼
事件循环 (produce)
  │
  ├─→ drain() - 读取事件
  │     ├─→ observer_->wait() - 等待通知
  │     └─→ reader_->data_available() - 读取 journal
  │
  ├─→ on_active() - 每秒检查
  │     └─→ on_interval_check()
  │
  └─→ on_frame() - 定时器处理
        └─→ handle_timer_tasks()
```

---

## 总结

`kf_master` 启动过程可分为以下几个阶段：

1. **初始化阶段**：解析参数、创建核心对象（locator、location）
2. **IO层初始化**：创建 nanomsg socket（PUB/PULL）、初始化日志和 SQLite
3. **系统位置注册**：注册 master、cached、ledger 等系统服务位置
4. **OS信号处理**：注册全局信号处理器，实现优雅退出机制
5. **配置加载**：从 profile 数据库（WAL模式）加载预定义的 Location 和 Config
6. **Session 建立**：创建 master journal session 和 PUBLIC writer，写入 SessionStart 标记
7. **事件系统启动**：创建 Rx 事件流，注册所有事件处理器（注册、通道、时间、Ping等）
8. **事件循环运行**：进入主循环，处理事件、定时器、心跳和 session keep-alive

### master 核心职责

master 在系统中扮演**中心协调者**的角色，负责：

| 职责 | 说明 |
|------|------|
| 应用注册/注销管理 | 通过 `register_app()` 和 `deregister_app()` 管理所有应用的生命周期 |
| 通信通道建立与管理 | 处理 `RequestWriteTo`、`RequestReadFrom` 等请求，建立应用间通信 |
| 时间同步 | 通过 `TimeReset` 和 `TradingDay` 消息同步系统时钟 |
| 系统位置广播 | 维护 `locations_` 映射表，向所有应用广播位置信息 |
| 定时任务调度 | 通过 `timer_tasks_` 管理定时任务，支持 `TimeRequest` 请求 |
| Session 管理 | 通过 `session_builder_` 追踪应用活跃状态，实现 keep-alive 机制 |
| 优雅退出 | 通过信号处理和 `on_exit()` 确保所有资源正确清理 |

### 关键技术特点

- **Rx模式事件驱动**：使用 Reactive Extensions 实现事件流处理，支持链式操作
- **nanomsg IPC通信**：使用 PUB/PULL 模式实现进程间通信，支持低延迟模式
- **SQLite WAL模式**：配置存储使用 WAL 模式，支持并发读写
- **单例模式**：每个进程只能有一个 hero 实例，通过全局指针实现信号处理
- **journal-based 持久化**：所有事件通过 journal 文件持久化，支持回放和恢复