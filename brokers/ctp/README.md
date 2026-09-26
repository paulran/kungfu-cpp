# brokers/ctp — 上期技术 CTP (Comprehensive Transaction Platform) 扩展

`brokers/ctp` 将 [CTP][] 的行情 (`ThostFtdcMdApi`) 与交易
(`ThostFtdcTraderApi`) 封装为 kungfu 标准的 `broker::MarketData` /
`broker::Trader` 扩展，最终构建为 `libkf_ctp.so`（Linux）/
`kf_ctp.dll`（Windows），由 kf_md / kf_td 以 `--source ctp` 动态加载。

CTP 是期货行业事实上的统一柜台；本扩展用 SimNow、各期货公司生产前置
和 OpenCTP 等模拟/仿真环境测试通过。

使用CTP api v6.7.13_20260225 14:16:30.12079。

https://www.simnow.com.cn/DocumentDown/api_3/5_2_2/v6.7.13_20260225_traderSM.zip

[CTP](https://www.shfe.com.cn/sfit/memberbusiness/ctpsoftwareproduct/ctpsoftwaremain/) - [技术规范及下载](https://www.shfe.com.cn/services/technology/technical_download/)

[SimNow](https://www.simnow.com.cn/) - SimNow是上海期货交易所全资子公司上期技术公司专为投资者打造的期货模拟仿真交易平台，为上海期货交易所投资者教育网认证的期货模拟仿真系统。该[产品](https://www.simnow.com.cn/product.action)仿真各交易所的交易及结算规则研发，目前已经支持国内各期货交易所的商品期货业务。 [API下载](https://www.simnow.com.cn/static/apiDownload.action): 选择 期货期权 -> "PC" -> "请选择版本号"上选择对应的版本。 

[openctp](https://github.com/openctp/openctp) - 提供CTP股票期权、中泰证券XTP、华鑫证券奇点TORA、东方证券OST、东方财富证券EMT、盈透证券TWS、易盛TAP、量投QDP等各通道的CTPAPI兼容接口，CTP程序可以无缝对接各股票柜台。openctp也提供了一套基于TTS交易系统的模拟环境，同样提供了CTPAPI兼容接口，不仅支持国内期货与期权全品种，也支持A股股票、基金、债券以及股票期权模拟交易，可以替代Simnow。

---

## 目录结构

```
brokers/ctp/
├── CMakeLists.txt              # 构建 kf_ctp 共享库，链接 CTP 预编译 .so/.lib
├── api/                        # CTP 头文件（ThostFtdcMdApi/TraderApi/UserApiStruct…）
├── lib/                        # CTP 预编译共享库（无 lib 前缀）
│   ├── thostmduserapi_se.so    # Linux 行情库
│   └── thosttraderapi_se.so    # Linux 交易库
└── src/
    ├── ctp_ext_api.cpp         # 扩展入口：KF_EXTENSION_DEFINE_*_FACTORY
    ├── ctp_common.h/cpp        # 配置解析、枚举/时间/价格/哈希转换
    ├── ctp_md_spi.h/cpp        # 实现 CThostFtdcMdSpi，回调转发到 MarketDataCtp
    ├── ctp_trader_spi.h/cpp    # 实现 CThostFtdcTraderSpi，回调转发到 TraderCtp
    ├── market_data_ctp.h/cpp   # broker::MarketData 子类：订阅、Quote 转换
    └── trader_ctp.h/cpp        # broker::Trader 子类：下单/撤单/查询/回报
```

---

## 构建

### Linux / WSL

CTP 预编译 `.so` 已复制到 `brokers/ctp/lib/`。CTP 库不带 `lib` 前缀，链接时使用
GNU ld `-l:thostmduserapi_se.so` 精确匹配，并将 `brokers/ctp/lib` 写入
`BUILD_RPATH` / `INSTALL_RPATH`，保证 dlopen 时直接解析到依赖：

```bash
# 配置并构建扩展（同时重编 kungfu 依赖）
cmake -S kungfu-cpp -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build build --config Release --target kf_ctp kf_md kf_td -j2

# 产物：
#   build/Release/libkf_ctp.so        ← kf_md/kf_td 通过 plugin::Library 加载
#   build/Release/thostmduserapi_se.so
#   build/Release/thosttraderapi_se.so
```

### Windows / MSVC

CTP 的 Windows 分发包含 `thostmduserapi_se.dll/.lib`、
`thosttraderapi_se.dll/.lib`，请把 `.lib` / `.dll` 放到
`brokers/ctp/lib/` 下，`CMakeLists.txt` 的 MSVC 分支已预留
`target_link_directories(.../lib) + target_link_libraries(... thost*_se)`。
API 头文件（`ThostFtdc*.h`）版本须与所用 `.lib/.dll` 完全一致，否则
会因结构体大小差异崩溃。

---

## 运行前置要求

- **CTP 账号**：SimNow 注册 / 期货公司实盘 / OpenCTP 等
- **前置地址**：不同环境提供不同 `tcp://host:port`，见下表
- **账户类型**：当前实现以 `AccountType::Future` 注册（期货），支持
  期货 + 期权品种（期权按 CTP CombOffsetFlag/Hedge 标志在 `ctp_common`
  做映射）
- **日志目录可写**：CTP 需要可写目录保存 `*_flow.con` 等连接状态文件，
  扩展会自动创建 `<runtime folder>/ctp_td_flow` 和 `…/ctp_md_flow`
- **结算单确认**：`ReqQrySettlementInfoConfirm` 在登录成功后立即发起，
  通过后才标记 `BrokerState::Ready` 并允许下单；这一步失败通常表示
  **当日未用客户端登录确认过结算结果**

---

## 配置项

配置以 JSON 形式挂到对应 location（`md/<group>/<name>/live` 或
`td/<group>/<name>/live`）上的 `Config` 类型记录，写入 kungfu 的 profile
SQLite 后由 kf_cached 推给 app。`on_start()` 中 `parse_config(get_config())`
读取。

### JSON 字段

| 字段 | 必填 | 示例 | 说明 |
|---|---|---|---|
| `front_uri` | ✔ | `tcp://180.168.146.187:10202` | 前置地址；MD/TD 相同就都用此字段 |
| `broker_id` | ✔ | `9999` | 经纪公司代码，SimNow 固定为 9999 |
| `user_id` | ✔ | `123456` | 登录账号（SimNow 是 investorId，不是手机号） |
| `password` | ✔ | `******` | 登录密码（SimNow 需先在官网"改一次密码"才能用 API） |
| `app_id` | 生产/CTP 6.3+ 需 | `simnow_client_test` | 客户端产品编号，SimNow 固定值 |
| `auth_code` | 对应 app_id | `0000000000000000`（16 个 0） | 授权码，期货公司按客户端产品签发 |
| `user_product_info` | ✘ | `kungfu/1.0` | 产品信息，部分风控前置会校验 |
| `investor_id` | ✘ | 同 user_id | 投资者编号，缺省取 user_id |

### 快捷配置工具：`tools/setup_simnow_config.py`

脚本把 7x24 SimNow 前置 + 9999 broker 参数一次性写入 profile SQLite，并
计算正确的 location uid（MurmurHash3_x86_32, seed=42）：

```bash
python3 tools/setup_simnow_config.py \
    --user-id YOUR_INVESTOR_ID \
    --password YOUR_PASSWORD \
    --group ctp --name simnow
```

常用参数：

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--group` / `--name` | `ctp` / `simnow` | 对应 `--group` / `--name` 启动参数 |
| `--broker-id` | `9999` | 期货公司经纪编号 |
| `--app-id` / `--auth-code` | SimNow 默认值 | 生产环境替换为期货公司颁发 |
| `--md-front` / `--td-front` | 7x24（:10212 / :10202） | 换第一套前置请手动覆盖 |

### SimNow 常用前置

| 环境 | TD | MD | 说明 |
|---|---|---|---|
| 第二套 · 7×24 | `tcp://180.168.146.187:10202` | `tcp://180.168.146.187:10212` | 随时可连，品种/数据有限 |
| 第一套 · 交易时段 | `tcp://180.168.146.187:10101` | `tcp://180.168.146.187:10111` | 仅 9:00-11:30 / 13:00-15:00 / 夜盘 时段，撮合与实盘一致 |

参考资源：<https://www.simnow.com.cn/product.action>（SimNow 官方说明）、
AlgoPlus CTP 封装 <https://gitee.com/AlgoPlus/AlgoPlus>。

---

## 启动方式

```bash
BIN=kungfu-cpp/build/Release

# MD（broker=CTP，group/name=ctp/simnow，须与 Config 行一致）
setsid nohup "$BIN/kf_md" --group ctp --name simnow --source ctp \
  > /tmp/kf_md.out 2>&1 < /dev/null &

# TD
setsid nohup "$BIN/kf_td" --group ctp --name simnow --source ctp \
  > /tmp/kf_td.out 2>&1 < /dev/null &
```

启动顺序建议 `kf_master → kf_cached → kf_md → kf_td → kf_ledger → kf_api`；
Config 表只在 kf_cached 存活时推给 MD/TD，启动前把上面配置写入 profile。

---

## 架构要点

### 线程模型（Spi 回调 → 事件循环）

CTP API 的回调运行在其内部 worker 线程，不能直接写 journal（kungfu 的
writer 是单线程对象）。因此扩展实现了最小化任务队列：

- Spi 子类（`CtpMdSpi` / `CtpTraderSpi`）回调只做捕获副本 + `enqueue(Task)`
- MD 用 1Hz 轮询（`try_subscribe`）+ 空闲 drained；TD 复用同样机制，并
  额外用时间循环 `drain_tasks()` 逐帧消费 CTP worker 线程投递的回调
- 这样 Order、Trade、Quote 的写入始终在 kungfu 事件循环同一线程

### 请求节流（CTP "每秒 1 次查询"限制）

CTP 柜台对 `ReqQry*` 做了 1 秒 1 次的硬节流，超过即返回错误；扩展做了：

- `push_query(Task)` 把查询放进 `query_queue_`
- 主循环以 `pump_query_queue()` 每 1.1s 执行至多 1 条查询
- 启动/登录/结算确认后会排队：`ReqQryInstrument`（合约主档）、
  `ReqQryTradingAccount`（资金）、`ReqQryInvestorPosition`（持仓）、
  `ReqQryOrder` + `ReqQryTrade`（今日历史订单与成交）

### 订单 ID 映射

kungfu 使用 `order_id`（uint64，全局唯一），CTP 只认 `OrderRef`（字符串）
和柜台端 `OrderSysID`。扩展维护两张表：

- `order_id_to_ref_info_`：由 kungfu 下单时生成 `OrderRef`，存于请求中
  并在 map 里记下 `instrument_id / exchange_id / source(策略uid)`
- `ref_to_order_id_`：CTP 回报只带 `OrderRef` 时反查 kungfu order_id
- 当 OrderSysID / FrontID / SessionID 都出现时，对非本 session 的回报
  （重连后重放 / 跨日已存在订单）用 `stable_hash64(ExchangeID+OrderSysID)`
  合成稳定 64-bit order id，与 ledger 的历史 order_id 一致

### 持仓分页合并

`ReqQryInvestorPosition` 返回多页，同一 `(InstrumentID, Direction)` 可能
分裂成多行 raw amount；扩展用 `pending_positions_` 累积 `PositionRow`
（同时保留 open_amount / open_volume / open_cost），收到 `bIsLast=true`
后合并 `flush_positions()` 写回 longfist `Position` 记录到 PUBLIC / SYNC。

### 回报 → longfist 枚举映射（`ctp_common`）

- `Direction ↔ Side`
- `CombOffsetFlag ↔ Offset`
- `HedgeFlag`
- `OrderStatus (ctp char) ↔ longfist::OrderStatus`
- `PriceType / TimeCondition / VolumeCondition`（请求侧由 ctp_common 决定
  `THOST_FTDC_TC_GFD` / `THOST_FTDC_VC_AV` 等默认值）
- `ctp_price(price)`：`DBL_MAX`/`-DBL_MAX` 当作"无价格"映射成 0.0
- `nano_from_ctp_time(day, hhmmss, millisec)`：本地时区 yyyymmdd+HH:MM:SS
  转 yijinjing epoch nano；失败回退为 `now_in_nano()`

---

## 常见问题

1. **登录 `ErrorID=7 未初始化`**：交易日切换后 CTP 服务端在导入昨日
   数据，稍后（通常 30s~2min）再登录即可；SimNow 7x24 环境通常不出现
2. **`BrokerState::LoginFailed` 且 ErrorID 指向账号/密码错误**：
   - SimNow 注册后**需要先改一次密码**再用于 API；
   - `broker_id`、`front_uri` 必须与账号所属环境匹配（7x24 / 交易时段是
     两套完全独立的服务器，账号不互通）
3. **`front_uri is empty` 报错**：Config 行未写入或 location 不匹配。用
   `tools/inspect_profile_db.py` 检查 `config` 表，并核对
   `md/<group>/<name>/live` / `td/<group>/<name>/live` 的 uid 与启动参数
4. **连上后很快 `FrontDisconnected reason=8194`**：版本不匹配，CTP 头文件
   与 `.so/.dll` 版本不一致会握手失败，请整包替换
5. **下单返回 `OrderActionError` 且描述是"一秒内请求过多"**：通常是查询
   线程与下单并发，检查日志确认是 `ReqQry*` 还是 `ReqOrderInsert*` 触发。
   扩展已对查询做了节流，如仍出现请加日志定位具体请求
6. **持仓为空或只显示一部分**：CTP 的 `ReqQryInvestorPosition` 只有"成交
   过"（至少 1 笔开仓或有历史挂单）的合约/方向才会返回记录；纯今日新
   挂单若未成交可能不回

---

## 构建陷阱备忘

1. CTP `.so` 无 `lib` 前缀 → 用 GNU ld `-l:thostmduserapi_se.so`，不要
   直接给全路径（CMake 会退化为 `-l thostmduserapi_se` 导致 ld 找不到）
2. `CThostFtdcInputOrderField` / `QrySettlementInfoConfirmField` 等 char
   数组字段不能直接 `= "str"` 赋值，必须 `strncpy(… sizeof(…)-1)`
3. `ReqQrySettlementInfoConfirm` 的参数类型是
   `CThostFtdcQrySettlementInfoConfirmField`（前缀 `Qry`，不是
   `CThostFtdcSettlementInfoConfirmField`）
4. MD 订阅 `SubscribeMarketData` 需要 `char*[]` + `count`，内部持有的是
   instrument_id 字符串引用，`MarketDataCtp::do_subscribe()` 里把
   C++ string 向量复制成指针缓冲区，等 `SubscribeMarketData` 返回后再
   释放，别悬空
5. `libkf_ctp.so` 的 `ldd libkf_ctp.so` 中 `libkungfu.so => not found`
   是**正常现象**：宿主 kf_td / kf_md 会先把 libkungfu.so 加载到进程，
   再 `dlopen(libkf_ctp.so)`；`tools/smoke_ctp_ext.sh` 会把 `Release/`
   加入 `LD_LIBRARY_PATH` 做一次完整 dlopen 验证
