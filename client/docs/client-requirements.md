# KungFu-CPP 客户端功能需求文档

---

## 一、项目背景

KungFu-CPP 是 [kungfu-origin](https://github.com/kungfu-origin/kungfu) 量化交易执行系统的纯 C++20 重写版本。原项目使用 Electron + Vue 3 作为客户端，新项目采用 Qt 6 作为客户端框架，通过 REST API + WebSocket 与后端 API Gateway（`kf_api`）通信。

客户端为独立子项目，不依赖 kungfu-cpp 核心库，仅通过网络协议接入交易系统。

---

## 二、系统架构

```
┌─────────────────────────────────────────────────────────┐
│                   Qt 客户端                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐             │
│  │ REST API │  │WebSocket │  │ UI 界面   │             │
│  │  客户端   │  │  客户端   │  │ (Widgets) │             │
│  └────┬─────┘  └────┬─────┘  └──────────┘             │
└───────┼──────────────┼──────────────────────────────────┘
        │ HTTP/JSON    │ WS/JSON
        ▼              ▼
┌─────────────────────────────────────────────────────────┐
│              API Gateway (kf_api)                        │
│  HTTP: http://{host}:{port}/api/v1/                     │
│  WS:   ws://{host}:{port}/api/v1/ws                     │
└─────────────────────────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────────┐
│     核心服务层 (Master / Ledger / MD / TD / Strategy)    │
└─────────────────────────────────────────────────────────┘
```

---

## 三、功能模块详细说明

### 3.1 认证登录

| 功能项 | 说明 |
|--------|------|
| 用户登录 | 输入服务地址、用户名、密码，获取 JWT Token |
| Token 管理 | 自动保存 Token，所有后续请求携带 Bearer Token |
| 登录失败处理 | 展示错误信息（密码错误、网络不可达等） |
| Token 过期处理 | 收到 401 时提示重新登录 |

**协议接口：**
```
POST /api/v1/auth/login
请求: {"username": "admin", "password": "admin"}
成功: {"token": "eyJ...", "expires_in": 86400, "token_type": "Bearer"}
失败: {"error": "Invalid credentials"}
```

---

### 3.2 系统监控

| 功能项 | 说明 |
|--------|------|
| 进程列表 | 展示所有注册进程（Master、Ledger、Cached、MD、TD、Strategy、API） |
| 进程状态 | 显示每个进程的类别、分组、名称、运行模式、连接状态 |
| 实时刷新 | 通过 WebSocket `system.status` 频道实时更新 |
| 定时轮询 | 每 5 秒通过 REST API 刷新完整状态 |

**数据字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| uid | uint32 | 进程唯一 ID |
| category | int | 进程类别（0=系统, 1=交易, 2=行情, 3=策略） |
| group | string | 进程组名（如 "sim", "ctp"） |
| name | string | 进程实例名 |
| mode | int | 运行模式（0=实盘, 1=数据, 2=回放, 3=回测） |
| broker_state | int | 柜台状态（0=未知, 1=空闲, 2=断开, 3=已连, 4=登录, 5=就绪, 6=登录失败） |

**协议接口：**
```
GET /api/v1/system/status
响应: [{"uid":123, "category":1, "group":"ctp", "name":"account1", "mode":0, "broker_state":5}, ...]
```

---

### 3.3 账户管理

#### 3.3.1 账户列表

| 功能项 | 说明 |
|--------|------|
| 账户列表 | 展示所有已注册的交易网关（TD）账户 |
| 连接状态 | 每个账户显示柜台连接状态 |
| 账户切换 | 选择不同账户查看对应的资金和持仓 |

**数据字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| uid | uint32 | TD 进程唯一 ID |
| source | string | 柜台来源（"ctp", "sim", "xtp"） |
| account_id | string | 账户 ID |
| state | int | 柜台连接状态 |

**协议接口：**
```
GET /api/v1/accounts
响应: [{"uid":123, "source":"ctp", "account_id":"080001", "state":5}, ...]
```

#### 3.3.2 资金概览

| 功能项 | 说明 |
|--------|------|
| 初始权益 | 当日初始权益 |
| 动态权益 | 实时净值 |
| 可用资金 | 可用于下单的资金 |
| 保证金 | 已用保证金 |
| 冻结资金 | 冻结资金 + 冻结保证金 + 冻结手续费 |
| 已实现盈亏 | 已平仓盈亏 |
| 浮动盈亏 | 未平仓持仓浮盈 |
| 实时更新 | 通过 WebSocket `asset.{account_id}` 频道推送 |

**数据字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| account_id | string | 账户 ID |
| initial_equity | double | 初始权益 |
| static_equity | double | 静态权益 |
| dynamic_equity | double | 动态权益 |
| available | double | 可用资金 |
| margin | double | 已用保证金 |
| frozen_cash | double | 冻结资金 |
| frozen_margin | double | 冻结保证金 |
| frozen_fee | double | 冻结手续费 |
| realized_pnl | double | 已实现盈亏 |
| unrealized_pnl | double | 未实现盈亏 |

**协议接口：**
```
GET /api/v1/accounts/{account_id}/assets
响应: {"account_id":"080001", "initial_equity":1000000.0, "dynamic_equity":1008500.0, ...}
错误: {"error": "Account not found"}
```

#### 3.3.3 持仓管理

| 功能项 | 说明 |
|--------|------|
| 持仓列表 | 展示指定账户的所有持仓 |
| 多空方向 | 区分多头/空头持仓 |
| 今仓/昨仓 | 显示总持仓量和昨仓量 |
| 开仓均价 | 平均开仓价格 |
| 浮动盈亏 | 单个持仓的未实现盈亏 |
| 实时更新 | 通过 WebSocket `position.{account_id}` 频道推送 |

**数据字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| instrument_id | string | 合约代码 |
| exchange_id | string | 交易所代码 |
| direction | int | 持仓方向（0=多, 1=空） |
| volume | int64 | 持仓量 |
| yesterday_volume | int64 | 昨仓量 |
| avg_open_price | double | 开仓均价 |
| position_cost | double | 持仓成本 |
| unrealized_pnl | double | 未实现盈亏 |
| realized_pnl | double | 已实现盈亏 |

**协议接口：**
```
GET /api/v1/accounts/{account_id}/positions
响应: [{"instrument_id":"IF2312", "exchange_id":"CFFEX", "direction":0, "volume":5, ...}, ...]
```

---

### 3.4 订单管理

#### 3.4.1 下单（提交订单）

| 功能项 | 说明 |
|--------|------|
| 合约代码 | 输入或选择合约代码（最长 31 字符） |
| 交易所 | 选择交易所（SSE/SZSE/CFFEX/SHFE/DCE/CZCE/INE/GFEX） |
| 价格 | 输入限价（市价单填 0） |
| 数量 | 输入委托数量（必须 > 0） |
| 买卖方向 | 选择买入(0)或卖出(1) |
| 开平标志 | 选择开仓(0)/平仓(1)/平今(2)/平昨(3) |
| 价格类型 | 选择限价(0)/市价(1)/最优价(2) |
| 参数校验 | 合约非空、数量大于0 |

**协议接口：**
```
POST /api/v1/orders
请求: {"instrument_id":"600000", "exchange_id":"SSE", "limit_price":10.5, "volume":100, "side":0, "offset":0, "price_type":0}
成功: {"order_id":1716750000000000, "status":"submitted"}
失败: {"error":"Volume must be positive"} 或 {"error":"No TD service available"}
```

#### 3.4.2 委托列表

| 功能项 | 说明 |
|--------|------|
| 订单列表 | 展示所有委托订单 |
| 订单状态 | 显示实时状态（已提交/待成交/已撤/错误/全成/部成） |
| 成交进度 | 显示委托量/已成量/未成量 |
| 撤单操作 | 选中订单后可撤单（仅活跃订单可撤） |
| 实时更新 | 通过 WebSocket `order.{account_id}` 频道推送 |

**数据字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| order_id | uint64 | 订单 ID |
| instrument_id | string | 合约代码 |
| exchange_id | string | 交易所代码 |
| limit_price | double | 限价 |
| frozen_price | double | 冻结价格 |
| volume | int64 | 委托数量 |
| volume_traded | int64 | 已成交数量 |
| volume_left | int64 | 剩余数量 |
| status | int | 订单状态 |
| side | int | 买卖方向 |
| offset | int | 开平标志 |
| insert_time | int64 | 委托时间（纳秒时间戳） |
| update_time | int64 | 最后更新时间（纳秒时间戳） |

**协议接口：**
```
GET /api/v1/orders               -- 查询所有订单
GET /api/v1/orders/{order_id}    -- 查询单笔订单
DELETE /api/v1/orders/{order_id} -- 撤销订单
```

#### 3.4.3 成交记录

| 功能项 | 说明 |
|--------|------|
| 成交列表 | 展示所有成交回报 |
| 实时更新 | 新成交实时插入列表顶部 |
| 关联订单 | 显示成交对应的订单号 |

**数据字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| trade_id | uint64 | 成交 ID |
| order_id | uint64 | 对应订单 ID |
| instrument_id | string | 合约代码 |
| exchange_id | string | 交易所代码 |
| price | double | 成交价格 |
| volume | int64 | 成交数量 |
| side | int | 买卖方向 |
| offset | int | 开平标志 |
| trade_time | int64 | 成交时间（纳秒时间戳） |

**WebSocket 推送：**
```json
{"channel": "trade.080001", "data": {"trade_id":20231215100001, "order_id":1716750000000000, ...}}
```

---

### 3.5 行情管理

#### 3.5.1 行情订阅

| 功能项 | 说明 |
|--------|------|
| 订阅合约 | 输入合约代码和交易所，发起行情订阅 |
| 取消订阅 | 取消已订阅合约的行情推送 |
| 合约列表查询 | 获取所有已缓存的合约信息 |

**协议接口：**
```
POST /api/v1/market/subscribe
请求: {"instrument_id":"600000", "exchange_id":"SSE", "instrument_type":1}
成功: {"status":"subscribed", "instrument_id":"600000", "exchange_id":"SSE"}
失败: {"error":"No MD service available"}

POST /api/v1/market/unsubscribe
请求: {"instrument_id":"600000", "exchange_id":"SSE"}
成功: {"status":"unsubscribed", "instrument_id":"600000", "exchange_id":"SSE"}

GET /api/v1/market/instruments
响应: [{"instrument_id":"IF2312", "exchange_id":"CFFEX", "instrument_type":2, "price_tick":0.2, ...}, ...]
```

#### 3.5.2 实时行情展示

| 功能项 | 说明 |
|--------|------|
| 行情表格 | 展示已订阅合约的实时行情 |
| 最新价 | 实时更新最新成交价 |
| 涨跌幅 | 基于昨收价计算百分比涨跌 |
| 五档盘口 | 买一~买五价/量、卖一~卖五价/量 |
| 成交量/额 | 当日累计成交量和成交额 |
| 实时更新 | 通过 WebSocket `quote.{instrument_id}` 频道推送 |

**行情数据字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| instrument_id | string | 合约代码 |
| exchange_id | string | 交易所代码 |
| data_time | int64 | 行情时间（纳秒时间戳） |
| last_price | double | 最新价 |
| pre_close_price | double | 昨收价 |
| open_price | double | 开盘价 |
| high_price | double | 最高价 |
| low_price | double | 最低价 |
| volume | int64 | 成交量 |
| turnover | double | 成交额 |
| bid_price_0 ~ bid_price_4 | double | 买一~买五价 |
| bid_volume_0 ~ bid_volume_4 | int64 | 买一~买五量 |
| ask_price_0 ~ ask_price_4 | double | 卖一~卖五价 |
| ask_volume_0 ~ ask_volume_4 | int64 | 卖一~卖五量 |

---

### 3.6 策略管理

| 功能项 | 说明 |
|--------|------|
| 策略列表 | 展示所有已注册的策略进程 |
| 策略状态 | 显示策略运行状态 |
| 定时刷新 | 通过 REST API 定时获取策略列表 |

**数据字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| uid | uint32 | 策略进程唯一 ID |
| group | string | 策略组名 |
| name | string | 策略实例名 |
| state | int | 状态（可选） |

**协议接口：**
```
GET /api/v1/strategies
响应: [{"uid":456789012, "group":"strategy", "name":"alpha01", "state":5}, ...]
```

---

### 3.7 WebSocket 实时推送

#### 3.7.1 连接与订阅

| 功能项 | 说明 |
|--------|------|
| 连接 | 标准 RFC 6455 WebSocket 连接到 `ws://{host}:{port}/api/v1/ws` |
| 订阅频道 | 发送 `{"action":"subscribe","channel":"<name>"}` |
| 取消订阅 | 发送 `{"action":"unsubscribe","channel":"<name>"}` |
| 通配订阅 | 使用 `"*"` 订阅所有频道 |
| 自动重连 | 断线后 3 秒自动重连，重连后重新订阅所有频道 |

#### 3.7.2 推送频道

| 频道格式 | 说明 | 推送时机 |
|----------|------|----------|
| `quote.{instrument_id}` | 实时行情快照 | 收到新行情时 |
| `order.{account_id}` | 订单状态变化 | 订单状态更新时 |
| `trade.{account_id}` | 成交回报 | 新成交产生时 |
| `position.{account_id}` | 持仓变化 | 持仓更新时 |
| `asset.{account_id}` | 资金变化 | 资产更新时 |
| `system.status` | 系统状态变化 | 进程注册/状态变更时 |

#### 3.7.3 消息格式

```json
// 订阅请求
{"action": "subscribe", "channel": "quote.600000"}

// 订阅确认
{"action": "subscribed", "channel": "quote.600000"}

// 数据推送
{"channel": "quote.600000", "data": {...}}
```

---

## 四、UI 界面设计

### 4.1 界面布局

采用 QMainWindow + QDockWidget 可拖拽面板布局，用户可自由拖拽、浮动、关闭各面板。

```
┌──────────────────────────────────────────────────────────────┐
│  菜单栏: 文件(登录/退出) | 视图(面板切换) | 帮助(关于)        │
├────────────────────────────────────┬─────────────────────────┤
│                                    │                         │
│         行情面板                    │      下单面板            │
│   (实时行情表 + 订阅工具栏)         │   (表单: 合约/价格/     │
│                                    │    数量/方向/开平/类型)  │
│                                    │                         │
├────────────────────────────────────┼─────────────────────────┤
│                                    │                         │
│    委托面板 │ 成交面板 │ 持仓面板    │   资金面板              │
│    (Tab 切换)                      │   策略面板              │
│                                    │   系统面板              │
│                                    │   (Tab 切换)            │
├────────────────────────────────────┴─────────────────────────┤
│  状态栏: 连接状态 / 操作提示                                  │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 面板列表

| 面板 | 功能 | 组件类型 |
|------|------|----------|
| 行情面板 | 实时行情表 + 合约订阅工具栏 | QTableView + 工具栏 |
| 下单面板 | 订单提交表单 | QFormLayout |
| 委托面板 | 活跃订单表 + 撤单按钮 | QTableView + 按钮 |
| 成交面板 | 成交记录表 | QTableView |
| 持仓面板 | 持仓表 + 账户选择 | QTableView + QComboBox |
| 资金面板 | 资金概览 + 账户选择 | QFormLayout + QComboBox |
| 策略面板 | 策略列表表 | QTableWidget |
| 系统面板 | 进程状态表 | QTableView |
| 登录对话框 | 服务地址/用户名/密码输入 | QDialog |

---

## 五、枚举值定义

### 5.1 买卖方向 (Side)

| 值 | 名称 | 中文 |
|----|------|------|
| 0 | Buy | 买入 |
| 1 | Sell | 卖出 |

### 5.2 开平标志 (Offset)

| 值 | 名称 | 中文 |
|----|------|------|
| 0 | Open | 开仓 |
| 1 | Close | 平仓 |
| 2 | CloseToday | 平今 |
| 3 | CloseYesterday | 平昨 |

### 5.3 持仓方向 (Direction)

| 值 | 名称 | 中文 |
|----|------|------|
| 0 | Long | 多头 |
| 1 | Short | 空头 |

### 5.4 价格类型 (PriceType)

| 值 | 名称 | 中文 |
|----|------|------|
| 0 | Limit | 限价 |
| 1 | Market | 市价 |
| 2 | BestPrice | 最优价 |

### 5.5 订单状态 (OrderStatus)

| 值 | 名称 | 中文 |
|----|------|------|
| 0 | Unknown | 未知 |
| 1 | Submitted | 已提交 |
| 2 | Pending | 待成交 |
| 3 | Cancelled | 已撤销 |
| 4 | Error | 错误 |
| 5 | Filled | 全部成交 |
| 6 | PartialFilledNotActive | 部分成交已撤 |
| 7 | PartialFilledActive | 部分成交仍在 |

### 5.6 合约类型 (InstrumentType)

| 值 | 名称 | 中文 |
|----|------|------|
| 0 | Unknown | 未知 |
| 1 | Stock | 股票 |
| 2 | Future | 期货 |
| 3 | Bond | 债券 |
| 4 | StockOption | 股票期权 |
| 5 | Fund | 基金 |
| 6 | Index | 指数 |
| 7 | Repo | 回购 |
| 8 | Crypto | 数字货币 |

### 5.7 柜台状态 (BrokerState)

| 值 | 名称 | 中文 |
|----|------|------|
| 0 | Unknown | 未知 |
| 1 | Idle | 空闲 |
| 2 | DisConnected | 已断开 |
| 3 | Connected | 已连接 |
| 4 | LoggedIn | 已登录 |
| 5 | Ready | 就绪 |
| 6 | LoginFailed | 登录失败 |

### 5.8 进程类别 (Category)

| 值 | 名称 | 中文 |
|----|------|------|
| 0 | System | 系统服务 |
| 1 | TD | 交易网关 |
| 2 | MD | 行情网关 |
| 3 | Strategy | 策略进程 |

### 5.9 交易所代码

| 代码 | 说明 |
|------|------|
| SSE | 上海证券交易所 |
| SZSE | 深圳证券交易所 |
| CFFEX | 中国金融期货交易所 |
| SHFE | 上海期货交易所 |
| DCE | 大连商品交易所 |
| CZCE | 郑州商品交易所 |
| INE | 上海国际能源交易中心 |
| GFEX | 广州期货交易所 |

---

## 六、完整协议接口一览

| # | 方法 | 路径 | 认证 | 说明 |
|---|------|------|:---:|------|
| 1 | POST | `/api/v1/auth/login` | 否 | 用户登录，获取 JWT Token |
| 2 | GET | `/api/v1/system/status` | 是 | 系统进程状态列表 |
| 3 | GET | `/api/v1/accounts` | 是 | 交易账户列表 |
| 4 | GET | `/api/v1/accounts/{id}/assets` | 是 | 账户资金查询 |
| 5 | GET | `/api/v1/accounts/{id}/positions` | 是 | 账户持仓查询 |
| 6 | GET | `/api/v1/orders` | 是 | 全部委托订单 |
| 7 | GET | `/api/v1/orders/{id}` | 是 | 单笔订单详情 |
| 8 | POST | `/api/v1/orders` | 是 | 提交新订单 |
| 9 | DELETE | `/api/v1/orders/{id}` | 是 | 撤销订单 |
| 10 | GET | `/api/v1/market/instruments` | 是 | 合约列表 |
| 11 | POST | `/api/v1/market/subscribe` | 是 | 订阅行情 |
| 12 | POST | `/api/v1/market/unsubscribe` | 是 | 取消订阅 |
| 13 | GET | `/api/v1/strategies` | 是 | 策略列表 |
| 14 | WS | `/api/v1/ws` | 否 | WebSocket 实时推送 |

---

## 七、错误处理

### 7.1 HTTP 错误响应格式

所有错误统一返回：
```json
{"error": "<错误描述>", "status": <HTTP状态码>}
```

| HTTP 状态码 | 含义 | 客户端处理 |
|:-----------:|------|-----------|
| 400 | 请求参数错误 | 展示错误信息，提示用户修正输入 |
| 401 | 未认证/Token 无效 | 弹出登录对话框 |
| 404 | 资源不存在 | 展示"未找到"提示 |
| 500 | 服务器内部错误 | 展示错误信息，建议重试 |

### 7.2 网络异常处理

| 场景 | 处理方式 |
|------|----------|
| 连接超时 | 状态栏提示，自动重试 |
| WebSocket 断开 | 3 秒后自动重连，重连后重新订阅 |
| 服务不可达 | 登录对话框显示错误，提示检查服务地址 |

---

## 八、配置

客户端连接的 API Gateway 在 `kungfu.toml` 配置文件中配置：

```toml
[api]
host = "127.0.0.1"          # 监听地址
port = 8080                  # HTTP REST 端口
ws_port = 8081               # NNG WebSocket 推送端口（内部客户端用）
jwt_secret = "your-secret"   # JWT 签名密钥
jwt_expire_hours = 24        # Token 过期时间（小时）
admin_user = "admin"         # 管理员用户名
admin_password = "admin"     # 管理员密码
```

客户端默认连接地址：`http://127.0.0.1:8080/api/v1`，可在登录对话框中修改。

---

## 九、技术选型

| 项目 | 选择 | 说明 |
|------|------|------|
| 语言 | C++20 | 与后端一致 |
| UI 框架 | Qt 6 Widgets | 桌面原生性能，LGPL 授权 |
| HTTP 客户端 | QNetworkAccessManager | Qt 内置，异步非阻塞 |
| WebSocket | QTcpSocket + RFC 6455 | 手动实现协议帧，无需额外依赖 |
| JSON 解析 | QJsonDocument | Qt 内置 |
| 构建系统 | CMake 3.20+ | 跨平台 |
| 数据展示 | QAbstractTableModel + QTableView | 标准 Qt MVC 模式 |
| 布局 | QDockWidget | 可拖拽、浮动、重排 |

---

## 十、与 kungfu-origin 客户端功能对比

| 功能 | kungfu-origin (Electron) | kungfu-cpp (Qt) |
|------|--------------------------|-----------------|
| 行情展示 | ✅ 实时行情表 | ✅ 实时行情表 |
| 下单交易 | ✅ 丰富表单 + 快捷按钮 | ✅ 表单（合约/价/量/方向/开平/类型） |
| 委托管理 | ✅ 订单表 + 撤单 | ✅ 订单表 + 撤单 |
| 成交记录 | ✅ 成交表 | ✅ 成交表 |
| 持仓管理 | ✅ 多空持仓 | ✅ 多空持仓 |
| 资金概览 | ✅ 资产详情 | ✅ 资产详情 |
| 策略管理 | ✅ 启停/参数/编辑 | ✅ 列表/状态（后续扩展启停） |
| 系统监控 | ✅ 进程控制中心 | ✅ 进程状态表 |
| 盘口深度 | ✅ Level2 OrderBook | 后续扩展 |
| 大宗交易 | ✅ BlockTrade | 后续扩展 |
| 期货套利 | ✅ FutureArbitrage | 后续扩展 |
| 算法交易 | ✅ TradingTask | 后续扩展 |
| 风控设置 | ✅ 价格偏离/仓位比例预警 | 后续扩展 |
| 代码编辑 | ✅ Monaco Editor | 后续扩展 |
| 国际化 | ✅ 中/英 | 当前中文，后续扩展 |
| 拖拽布局 | ✅ 自定义面板 | ✅ QDockWidget 拖拽 |
