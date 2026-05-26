# API Gateway 协议文档

## 概述

KungFu API Gateway (`kf_api`) 提供 RESTful HTTP 接口和 WebSocket 实时推送服务，供 Qt UI 或第三方客户端接入交易系统。

- **HTTP 基地址**: `http://{host}:{port}/api/v1/`
- **WebSocket (NNG pub/sub)**: `ws://{host}:{ws_port}/`
- **内容类型**: 所有请求和响应均为 `application/json`
- **认证方式**: JWT Bearer Token

---

## 1. 认证

### 1.1 登录

获取 JWT Token。

**请求**

```
POST /api/v1/auth/login
Content-Type: application/json
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `username` | string | 是 | 用户名 |
| `password` | string | 是 | 密码 |

**请求示例**

```json
{
    "username": "admin",
    "password": "admin"
}
```

**成功响应** `200 OK`

| 字段 | 类型 | 说明 |
|------|------|------|
| `token` | string | JWT Token，后续请求中在 Header 中携带 |
| `expires_in` | int | Token 有效期（秒） |
| `token_type` | string | 固定值 `"Bearer"` |

```json
{
    "token": "eyJhbGciOiJIUzI1NiJ9.eyJ1c2VyIjoiYWRtaW4iLCJleHAiOjE3MTY3NTAwMDB9.signature",
    "expires_in": 86400,
    "token_type": "Bearer"
}
```

**失败响应** `401 Unauthorized`

```json
{
    "error": "Invalid credentials"
}
```

### 1.2 认证方式

所有除 `/api/v1/auth/login` 之外的接口均需在 HTTP Header 中携带 JWT Token：

```
Authorization: Bearer <token>
```

未携带或 Token 无效/过期时返回：

```
HTTP/1.1 401 Unauthorized
Content-Type: application/json

{
    "error": "Unauthorized",
    "status": 401
}
```

---

## 2. 系统管理

### 2.1 获取系统状态

查询所有已注册进程的状态。

**请求**

```
GET /api/v1/system/status
Authorization: Bearer <token>
```

无请求参数。

**响应** `200 OK`

返回 JSON 数组，每个元素代表一个注册的进程：

| 字段 | 类型 | 说明 |
|------|------|------|
| `uid` | uint32 | 进程唯一 ID |
| `category` | int | 进程类别（见[进程类别枚举](#进程类别-category)） |
| `group` | string | 进程组名（如行情源名称 `"sim"`、柜台名称 `"ctp"`） |
| `name` | string | 进程实例名（如账户 ID `"test_account"`） |
| `mode` | int | 运行模式（0=LIVE, 1=DATA, 2=REPLAY, 3=BACKTEST） |
| `broker_state` | int | 柜台连接状态（见[柜台状态枚举](#柜台状态-brokerstate)），-1 表示未知 |

**响应示例**

```json
[
    {
        "uid": 123456789,
        "category": 1,
        "group": "ctp",
        "name": "my_account",
        "mode": 0,
        "broker_state": 5
    },
    {
        "uid": 987654321,
        "category": 3,
        "group": "strategy",
        "name": "alpha01",
        "mode": 0,
        "broker_state": -1
    }
]
```

---

## 3. 账户管理

### 3.1 获取账户列表

列出所有已注册的交易网关（TD）账户。

**请求**

```
GET /api/v1/accounts
Authorization: Bearer <token>
```

无请求参数。

**响应** `200 OK`

| 字段 | 类型 | 说明 |
|------|------|------|
| `uid` | uint32 | TD 进程唯一 ID |
| `source` | string | 柜台来源（如 `"ctp"`, `"sim"`, `"xtp"`） |
| `account_id` | string | 账户 ID |
| `state` | int | 柜台连接状态（见[柜台状态枚举](#柜台状态-brokerstate)） |

**响应示例**

```json
[
    {
        "uid": 123456789,
        "source": "ctp",
        "account_id": "080001",
        "state": 5
    }
]
```

### 3.2 获取账户资产

查询指定账户的资金状况。

**请求**

```
GET /api/v1/accounts/{account_id}/assets
Authorization: Bearer <token>
```

| 路径参数 | 类型 | 说明 |
|----------|------|------|
| `account_id` | string | 账户 ID |

**响应** `200 OK`

| 字段 | 类型 | 说明 |
|------|------|------|
| `account_id` | string | 账户 ID |
| `initial_equity` | double | 初始权益 |
| `static_equity` | double | 静态权益 |
| `dynamic_equity` | double | 动态权益（实时净值） |
| `available` | double | 可用资金 |
| `margin` | double | 已用保证金 |
| `frozen_cash` | double | 冻结资金 |
| `frozen_margin` | double | 冻结保证金 |
| `frozen_fee` | double | 冻结手续费 |
| `realized_pnl` | double | 已实现盈亏 |
| `unrealized_pnl` | double | 未实现盈亏 |

**响应示例**

```json
{
    "account_id": "080001",
    "initial_equity": 1000000.0,
    "static_equity": 1005000.0,
    "dynamic_equity": 1008500.0,
    "available": 800000.0,
    "margin": 150000.0,
    "frozen_cash": 50000.0,
    "frozen_margin": 0.0,
    "frozen_fee": 8.5,
    "realized_pnl": 5000.0,
    "unrealized_pnl": 3500.0
}
```

**失败响应** `200 OK`（账户不存在时）

```json
{
    "error": "Account not found"
}
```

### 3.3 获取账户持仓

查询指定账户的所有持仓。

**请求**

```
GET /api/v1/accounts/{account_id}/positions
Authorization: Bearer <token>
```

| 路径参数 | 类型 | 说明 |
|----------|------|------|
| `account_id` | string | 账户 ID |

**响应** `200 OK`

返回 JSON 数组，每个元素为一条持仓记录：

| 字段 | 类型 | 说明 |
|------|------|------|
| `instrument_id` | string | 合约代码 |
| `exchange_id` | string | 交易所代码（见[交易所代码](#交易所代码)） |
| `direction` | int | 持仓方向（见[方向枚举](#方向-direction)） |
| `volume` | int64 | 持仓量 |
| `yesterday_volume` | int64 | 昨仓量 |
| `avg_open_price` | double | 开仓均价 |
| `position_cost` | double | 持仓成本 |
| `unrealized_pnl` | double | 未实现盈亏 |
| `realized_pnl` | double | 已实现盈亏 |

**响应示例**

```json
[
    {
        "instrument_id": "IF2312",
        "exchange_id": "CFFEX",
        "direction": 0,
        "volume": 5,
        "yesterday_volume": 3,
        "avg_open_price": 3980.0,
        "position_cost": 19900.0,
        "unrealized_pnl": 500.0,
        "realized_pnl": 200.0
    }
]
```

---

## 4. 订单管理

### 4.1 查询订单列表

获取所有缓存的订单记录。

**请求**

```
GET /api/v1/orders
Authorization: Bearer <token>
```

无请求参数。

**响应** `200 OK`

返回 JSON 数组，每个元素为一条订单记录：

| 字段 | 类型 | 说明 |
|------|------|------|
| `order_id` | uint64 | 订单 ID |
| `instrument_id` | string | 合约代码 |
| `exchange_id` | string | 交易所代码 |
| `limit_price` | double | 限价 |
| `frozen_price` | double | 冻结价格 |
| `volume` | int64 | 委托数量 |
| `volume_traded` | int64 | 已成交数量 |
| `volume_left` | int64 | 剩余数量 |
| `status` | int | 订单状态（见[订单状态枚举](#订单状态-orderstatus)） |
| `side` | int | 买卖方向（见[买卖方向枚举](#买卖方向-side)） |
| `offset` | int | 开平标志（见[开平标志枚举](#开平标志-offset)） |
| `insert_time` | int64 | 委托时间（纳秒时间戳） |
| `update_time` | int64 | 最后更新时间（纳秒时间戳） |

**响应示例**

```json
[
    {
        "order_id": 1716750000000000,
        "instrument_id": "600000",
        "exchange_id": "SSE",
        "limit_price": 10.5,
        "frozen_price": 10.5,
        "volume": 100,
        "volume_traded": 50,
        "volume_left": 50,
        "status": 7,
        "side": 0,
        "offset": 0,
        "insert_time": 1716750000000000000,
        "update_time": 1716750001000000000
    }
]
```

### 4.2 查询单笔订单

根据订单 ID 查询订单详情。

**请求**

```
GET /api/v1/orders/{order_id}
Authorization: Bearer <token>
```

| 路径参数 | 类型 | 说明 |
|----------|------|------|
| `order_id` | uint64 | 订单 ID |

**成功响应** `200 OK`

字段同 [4.1 订单列表](#41-查询订单列表) 中的单个订单对象。

**失败响应** `404 Not Found`

```json
{
    "error": "Order not found"
}
```

### 4.3 提交订单

创建新的委托订单。

**请求**

```
POST /api/v1/orders
Authorization: Bearer <token>
Content-Type: application/json
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `instrument_id` | string | 是 | 合约代码（最长 31 字符） |
| `exchange_id` | string | 是 | 交易所代码（最长 15 字符） |
| `limit_price` | double | 是 | 限价（市价单可填 0） |
| `volume` | int64 | 是 | 委托数量（必须 > 0） |
| `side` | int | 是 | 买卖方向（见[买卖方向枚举](#买卖方向-side)） |
| `offset` | int | 是 | 开平标志（见[开平标志枚举](#开平标志-offset)） |
| `price_type` | int | 是 | 价格类型（见[价格类型枚举](#价格类型-pricetype)） |
| `order_id` | uint64 | 否 | 自定义订单 ID，不填则自动生成 |

**请求示例**

```json
{
    "instrument_id": "600000",
    "exchange_id": "SSE",
    "limit_price": 10.50,
    "volume": 100,
    "side": 0,
    "offset": 0,
    "price_type": 0
}
```

**成功响应** `201 Created`

| 字段 | 类型 | 说明 |
|------|------|------|
| `order_id` | uint64 | 分配的订单 ID |
| `status` | string | 固定值 `"submitted"` |

```json
{
    "order_id": 1716750000000000,
    "status": "submitted"
}
```

**失败响应** `400 Bad Request`

```json
{
    "error": "Volume must be positive"
}
```

```json
{
    "error": "No TD service available"
}
```

### 4.4 撤销订单

撤销指定的委托订单。

**请求**

```
DELETE /api/v1/orders/{order_id}
Authorization: Bearer <token>
```

| 路径参数 | 类型 | 说明 |
|----------|------|------|
| `order_id` | uint64 | 要撤销的订单 ID |

**成功响应** `200 OK`

| 字段 | 类型 | 说明 |
|------|------|------|
| `order_id` | uint64 | 订单 ID |
| `status` | string | 固定值 `"cancel_submitted"` |

```json
{
    "order_id": 1716750000000000,
    "status": "cancel_submitted"
}
```

**失败响应** `400 Bad Request`

```json
{
    "error": "No TD service available"
}
```

---

## 5. 行情管理

### 5.1 获取合约列表

查询所有已缓存的合约信息。

**请求**

```
GET /api/v1/market/instruments
Authorization: Bearer <token>
```

无请求参数。

**响应** `200 OK`

返回 JSON 数组，每个元素为一条合约信息：

| 字段 | 类型 | 说明 |
|------|------|------|
| `instrument_id` | string | 合约代码 |
| `exchange_id` | string | 交易所代码 |
| `instrument_type` | int | 合约类型（见[合约类型枚举](#合约类型-instrumenttype)） |
| `price_tick` | double | 最小变动价位 |
| `delivery_year` | int32 | 交割年（期货） |
| `delivery_month` | int32 | 交割月（期货） |
| `contract_multiplier` | int32 | 合约乘数 |
| `long_margin_ratio` | double | 多头保证金率 |
| `short_margin_ratio` | double | 空头保证金率 |

**响应示例**

```json
[
    {
        "instrument_id": "IF2312",
        "exchange_id": "CFFEX",
        "instrument_type": 2,
        "price_tick": 0.2,
        "delivery_year": 2023,
        "delivery_month": 12,
        "contract_multiplier": 300,
        "long_margin_ratio": 0.12,
        "short_margin_ratio": 0.12
    },
    {
        "instrument_id": "600000",
        "exchange_id": "SSE",
        "instrument_type": 1,
        "price_tick": 0.01,
        "delivery_year": 0,
        "delivery_month": 0,
        "contract_multiplier": 1,
        "long_margin_ratio": 1.0,
        "short_margin_ratio": 1.0
    }
]
```

### 5.2 订阅行情

订阅指定合约的实时行情推送。

**请求**

```
POST /api/v1/market/subscribe
Authorization: Bearer <token>
Content-Type: application/json
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `instrument_id` | string | 是 | 合约代码 |
| `exchange_id` | string | 是 | 交易所代码 |
| `instrument_type` | int | 否 | 合约类型（默认 0），见[合约类型枚举](#合约类型-instrumenttype) |

**请求示例**

```json
{
    "instrument_id": "600000",
    "exchange_id": "SSE",
    "instrument_type": 1
}
```

**成功响应** `200 OK`

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | 固定值 `"subscribed"` |
| `instrument_id` | string | 订阅的合约代码 |
| `exchange_id` | string | 订阅的交易所代码 |

```json
{
    "status": "subscribed",
    "instrument_id": "600000",
    "exchange_id": "SSE"
}
```

**失败响应** `200 OK`

```json
{
    "error": "No MD service available"
}
```

### 5.3 取消订阅

取消指定合约的行情订阅。

**请求**

```
POST /api/v1/market/unsubscribe
Authorization: Bearer <token>
Content-Type: application/json
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `instrument_id` | string | 是 | 合约代码 |
| `exchange_id` | string | 是 | 交易所代码 |

**请求示例**

```json
{
    "instrument_id": "600000",
    "exchange_id": "SSE"
}
```

**成功响应** `200 OK`

```json
{
    "status": "unsubscribed",
    "instrument_id": "600000",
    "exchange_id": "SSE"
}
```

**失败响应** `200 OK`

```json
{
    "error": "No MD service available"
}
```

---

## 6. 策略管理

### 6.1 获取策略列表

列出所有已注册的策略进程。

**请求**

```
GET /api/v1/strategies
Authorization: Bearer <token>
```

无请求参数。

**响应** `200 OK`

返回 JSON 数组，每个元素为一个策略：

| 字段 | 类型 | 说明 |
|------|------|------|
| `uid` | uint32 | 策略进程唯一 ID |
| `group` | string | 策略组名 |
| `name` | string | 策略实例名 |
| `state` | int | 柜台连接状态（仅在有状态时返回） |

**响应示例**

```json
[
    {
        "uid": 456789012,
        "group": "strategy",
        "name": "alpha01",
        "state": 5
    },
    {
        "uid": 456789013,
        "group": "strategy",
        "name": "grid_trader"
    }
]
```

---

## 7. WebSocket 实时推送

### 7.1 连接方式

**标准 WebSocket（外部客户端）**

```
ws://{host}:{port}/api/v1/ws
```

浏览器或 Qt 客户端通过标准 RFC 6455 WebSocket 协议连接。连接后通过 JSON 消息订阅/取消频道。

**NNG Pub/Sub WebSocket（内部客户端）**

```
ws://{host}:{ws_port}/
```

内部 C++ 服务通过 NNG `sub0` socket 连接。使用 NNG 消息主题前缀进行频道过滤。

### 7.2 订阅频道

连接后发送 JSON 消息订阅指定频道：

**订阅请求**

```json
{
    "action": "subscribe",
    "channel": "quote.600000"
}
```

**订阅确认**

```json
{
    "action": "subscribed",
    "channel": "quote.600000"
}
```

**取消订阅**

```json
{
    "action": "unsubscribe",
    "channel": "quote.600000"
}
```

**取消确认**

```json
{
    "action": "unsubscribed",
    "channel": "quote.600000"
}
```

使用 `"*"` 可订阅所有频道。

### 7.3 推送频道

| 频道格式 | 说明 | 推送时机 |
|----------|------|----------|
| `quote.{instrument_id}` | 实时行情快照 | 收到新行情时 |
| `order.{account_id}` | 订单状态变化 | 订单状态更新时 |
| `trade.{account_id}` | 成交回报 | 新成交产生时 |
| `position.{account_id}` | 持仓变化 | 持仓更新时 |
| `asset.{account_id}` | 资金变化 | 资产更新时 |
| `system.status` | 系统状态变化 | 进程注册/状态变更时 |

### 7.4 推送消息格式

所有推送消息统一格式：

```json
{
    "channel": "<channel_name>",
    "data": { ... }
}
```

#### 行情推送 `quote.{instrument_id}`

`data` 字段内容：

| 字段 | 类型 | 说明 |
|------|------|------|
| `instrument_id` | string | 合约代码 |
| `exchange_id` | string | 交易所代码 |
| `data_time` | int64 | 行情时间（纳秒时间戳） |
| `last_price` | double | 最新价 |
| `pre_close_price` | double | 昨收价 |
| `open_price` | double | 开盘价 |
| `high_price` | double | 最高价 |
| `low_price` | double | 最低价 |
| `volume` | int64 | 成交量 |
| `turnover` | double | 成交额 |
| `bid_price_0` ~ `bid_price_4` | double | 买一~买五价 |
| `bid_volume_0` ~ `bid_volume_4` | int64 | 买一~买五量 |
| `ask_price_0` ~ `ask_price_4` | double | 卖一~卖五价 |
| `ask_volume_0` ~ `ask_volume_4` | int64 | 卖一~卖五量 |

**推送示例**

```json
{
    "channel": "quote.600000",
    "data": {
        "instrument_id": "600000",
        "exchange_id": "SSE",
        "data_time": 1716750000000000000,
        "last_price": 10.52,
        "pre_close_price": 10.40,
        "open_price": 10.45,
        "high_price": 10.55,
        "low_price": 10.38,
        "volume": 5000000,
        "turnover": 52500000.0,
        "bid_price_0": 10.51,
        "bid_price_1": 10.50,
        "bid_price_2": 10.49,
        "bid_price_3": 10.48,
        "bid_price_4": 10.47,
        "bid_volume_0": 1200,
        "bid_volume_1": 3500,
        "bid_volume_2": 800,
        "bid_volume_3": 2000,
        "bid_volume_4": 1500,
        "ask_price_0": 10.52,
        "ask_price_1": 10.53,
        "ask_price_2": 10.54,
        "ask_price_3": 10.55,
        "ask_price_4": 10.56,
        "ask_volume_0": 900,
        "ask_volume_1": 2200,
        "ask_volume_2": 1800,
        "ask_volume_3": 4000,
        "ask_volume_4": 600
    }
}
```

#### 订单推送 `order.{account_id}`

`data` 字段内容同 [4.1 订单列表](#41-查询订单列表) 的订单对象。

#### 成交推送 `trade.{account_id}`

`data` 字段内容：

| 字段 | 类型 | 说明 |
|------|------|------|
| `trade_id` | uint64 | 成交 ID |
| `order_id` | uint64 | 对应订单 ID |
| `instrument_id` | string | 合约代码 |
| `exchange_id` | string | 交易所代码 |
| `price` | double | 成交价格 |
| `volume` | int64 | 成交数量 |
| `side` | int | 买卖方向 |
| `offset` | int | 开平标志 |
| `trade_time` | int64 | 成交时间（纳秒时间戳） |

**推送示例**

```json
{
    "channel": "trade.080001",
    "data": {
        "trade_id": 20231215100001,
        "order_id": 1716750000000000,
        "instrument_id": "600000",
        "exchange_id": "SSE",
        "price": 10.50,
        "volume": 100,
        "side": 0,
        "offset": 0,
        "trade_time": 1716750001000000000
    }
}
```

#### 持仓推送 `position.{account_id}`

`data` 字段内容同 [3.3 获取账户持仓](#33-获取账户持仓) 的持仓对象。

#### 资金推送 `asset.{account_id}`

`data` 字段内容同 [3.2 获取账户资产](#32-获取账户资产) 的资产对象。

---

## 8. 错误响应

所有错误响应统一格式：

```json
{
    "error": "<错误信息>",
    "status": <HTTP状态码>
}
```

| HTTP 状态码 | 说明 |
|-------------|------|
| 400 | 请求参数错误或业务逻辑错误 |
| 401 | 未认证或 Token 无效/过期 |
| 404 | 请求的资源不存在 |
| 500 | 服务器内部错误 |

---

## 9. 枚举值定义

### 买卖方向 (Side)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | Buy | 买入 |
| 1 | Sell | 卖出 |

### 开平标志 (Offset)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | Open | 开仓 |
| 1 | Close | 平仓 |
| 2 | CloseToday | 平今 |
| 3 | CloseYesterday | 平昨 |

### 方向 (Direction)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | Long | 多头 |
| 1 | Short | 空头 |

### 价格类型 (PriceType)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | Limit | 限价 |
| 1 | Market | 市价 |
| 2 | BestPrice | 最优价 |

### 订单状态 (OrderStatus)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | Unknown | 未知 |
| 1 | Submitted | 已提交 |
| 2 | Pending | 待成交 |
| 3 | Cancelled | 已撤销 |
| 4 | Error | 错误 |
| 5 | Filled | 全部成交 |
| 6 | PartialFilledNotActive | 部分成交已撤 |
| 7 | PartialFilledActive | 部分成交仍在 |

### 合约类型 (InstrumentType)

| 值 | 名称 | 说明 |
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

### 柜台状态 (BrokerState)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | Unknown | 未知 |
| 1 | Idle | 空闲 |
| 2 | DisConnected | 已断开 |
| 3 | Connected | 已连接 |
| 4 | LoggedIn | 已登录 |
| 5 | Ready | 就绪（可交易） |
| 6 | LoginFailed | 登录失败 |

### 进程类别 (Category)

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | SYSTEM | 系统服务（master, ledger, api） |
| 1 | TD | 交易网关 |
| 2 | MD | 行情网关 |
| 3 | STRATEGY | 策略进程 |

---

## 10. 交易所代码

| 代码 | 说明 |
|------|------|
| `SSE` | 上海证券交易所 |
| `SZSE` | 深圳证券交易所 |
| `CFFEX` | 中国金融期货交易所 |
| `SHFE` | 上海期货交易所 |
| `DCE` | 大连商品交易所 |
| `CZCE` | 郑州商品交易所 |
| `INE` | 上海国际能源交易中心 |
| `GFEX` | 广州期货交易所 |

---

## 11. 配置

API Gateway 在 `kungfu.toml` 配置文件的 `[api]` 段进行配置：

```toml
[api]
host = "127.0.0.1"          # 监听地址
port = 8080                  # HTTP REST 端口
ws_port = 8081               # NNG WebSocket 推送端口
jwt_secret = "your-secret"   # JWT 签名密钥（生产环境请修改）
jwt_expire_hours = 24        # Token 过期时间（小时）
admin_user = "admin"         # 管理员用户名
admin_password = "admin"     # 管理员密码（生产环境请修改）
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `host` | string | `"127.0.0.1"` | HTTP/WS 监听地址，设为 `"0.0.0.0"` 可允许外部访问 |
| `port` | uint16 | `8080` | REST API 端口 |
| `ws_port` | uint16 | `8081` | NNG WebSocket 推送端口 |
| `jwt_secret` | string | `"kungfu-default-secret"` | JWT HMAC-SHA256 签名密钥 |
| `jwt_expire_hours` | int | `24` | Token 有效期（小时） |
| `admin_user` | string | `"admin"` | 管理员用户名 |
| `admin_password` | string | `"admin"` | 管理员密码 |

---

## 12. CORS 支持

API Gateway 默认允许跨域请求，响应中包含：

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Headers: Authorization, Content-Type
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
```

`OPTIONS` 预检请求返回 `204 No Content`。

---

## 13. 完整接口一览

| 方法 | 路径 | 认证 | 说明 |
|------|------|------|------|
| POST | `/api/v1/auth/login` | 否 | 用户登录，获取 Token |
| GET | `/api/v1/system/status` | 是 | 系统进程状态 |
| GET | `/api/v1/accounts` | 是 | 账户列表 |
| GET | `/api/v1/accounts/{id}/assets` | 是 | 账户资产 |
| GET | `/api/v1/accounts/{id}/positions` | 是 | 账户持仓 |
| GET | `/api/v1/orders` | 是 | 订单列表 |
| GET | `/api/v1/orders/{id}` | 是 | 订单详情 |
| POST | `/api/v1/orders` | 是 | 提交订单 |
| DELETE | `/api/v1/orders/{id}` | 是 | 撤销订单 |
| GET | `/api/v1/market/instruments` | 是 | 合约列表 |
| POST | `/api/v1/market/subscribe` | 是 | 订阅行情 |
| POST | `/api/v1/market/unsubscribe` | 是 | 取消订阅 |
| GET | `/api/v1/strategies` | 是 | 策略列表 |
| WS | `/api/v1/ws` | 否 | WebSocket 实时推送（标准） |
| WS | `ws://{host}:{ws_port}/` | 否 | NNG pub/sub 推送（内部） |
