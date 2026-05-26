# API 网关功能清单

## 1. HTTP REST 服务 (Boost.Beast)

| 分类 | 接口 | 方法 | 说明 |
|------|------|------|------|
| **账户** | `/api/v1/accounts` | GET | 账户列表 |
| | `/api/v1/accounts/{id}` | GET | 账户详情 |
| | `/api/v1/accounts/{id}/assets` | GET | 账户资产 |
| | `/api/v1/accounts/{id}/positions` | GET | 账户持仓 |
| **订单** | `/api/v1/orders` | POST | 提交订单 |
| | `/api/v1/orders` | GET | 查询订单列表 |
| | `/api/v1/orders/{id}` | GET | 查询订单详情 |
| | `/api/v1/orders/{id}` | DELETE | 撤销订单 |
| **行情** | `/api/v1/market/subscribe` | POST | 订阅行情 |
| | `/api/v1/market/unsubscribe` | POST | 取消订阅 |
| | `/api/v1/market/instruments` | GET | 合约列表 |
| **策略** | `/api/v1/strategies` | POST | 启动策略 |
| | `/api/v1/strategies` | GET | 策略列表 |
| | `/api/v1/strategies/{id}` | GET | 策略详情 |
| | `/api/v1/strategies/{id}` | DELETE | 停止策略 |
| **系统** | `/api/v1/system/status` | GET | 各进程状态 |
| | `/api/v1/system/services/{name}/restart` | POST | 重启服务 |
| | `/api/v1/system/config` | GET/PUT | 系统配置 |

## 2. WebSocket 实时推送

| 通道 | 说明 |
|------|------|
| `quote.{instrument_id}` | 实时行情 |
| `order.{account_id}` | 订单状态变化 |
| `trade.{account_id}` | 成交推送 |
| `position.{account_id}` | 持仓变化 |
| `system.status` | 系统状态变化 |

## 3. 认证鉴权 (JWT)

- JWT Token 签发和验证
- 配置项：`jwt_secret`、`jwt_expire_hours`
- 所有 REST/WS 接口需验证 Token

## 4. 日志审计

- 记录所有 API 调用（时间、用户、接口、参数、结果）

## 5. 与核心层的集成

- 继承 `apprentice` 基类，作为系统内进程注册到 Master
- 通过 Journal (mmap) 读取行情、订单、成交、持仓数据
- 通过 Journal 写入 OrderInput（下单请求）
- 通过 NNG 与 Master 通信（查询进程状态、发送控制命令）

## 6. 架构要点

- **技术栈**: Boost.Beast (HTTP/WebSocket) + Boost.Asio (异步IO)
- **进程角色**: `category::SYSTEM`, group=`"service"`, name=`"api"`
- **启动优先级**: 6（最后启动，等所有服务就绪）
- **配置**: `[api]` 段 — host、port、ws_port、jwt_secret、jwt_expire_hours

## 7. 新增依赖

- 需要引入 **Boost.Beast**（目前 3rdparty 中没有 Boost，只有 Hana 独立子模块）
- 可能需要 OpenSSL（如果要支持 HTTPS/WSS）

## 8. 总结

API 网关本质上是一个"桥梁进程"——对外暴露 REST/WebSocket 接口供 Qt UI 或第三方客户端调用，对内通过 Journal 和 NNG 与交易系统的其他进程通信。核心工作量在 HTTP 路由分发、WebSocket 订阅管理、JWT 认证、以及 Journal 数据到 JSON 的转换（已有 Hana 序列化支持）。
