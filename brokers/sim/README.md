# brokers/sim — 内置模拟撮合 / 行情发生器

`brokers/sim` 是 kungfu 内置的**纯内存模拟**券商扩展，不依赖任何外部 API 或
网络，直接运行在 kf_md / kf_td 进程里，用于策略开发、回归测试和整体
集成验证。最终产物为 `libkf_sim.so`（Linux）/`kf_sim.dll`（Windows），
通过 `--source sim` 由宿主程序动态加载。

---

## 目录结构

```
brokers/sim/
├── CMakeLists.txt           # 构建 kf_sim 共享库
└── src/
    ├── sim_api.cpp           # 扩展入口：KF_EXTENSION_DEFINE_*_FACTORY
    ├── market_data_sim.h/cpp # 模拟行情：按频率生成 Quote 到 PUBLIC
    ├── trader_sim.h/cpp      # 模拟交易：按 match_mode 响应 OrderInput
    ├── order_book.h/cpp      # 简易 20 档盘口 & 撮合格子
    └── match_mode.h          # 撮合响应模式枚举
```

---

## 构建

和整个工程一并构建：

```bash
cmake --build build --config Release --target kf_sim kf_md kf_td -j2
# 产物：
#   build/Release/libkf_sim.so   (Linux)
#   build/Release/libkungfu.so   (宿主依赖)
```

`CMakeLists.txt` 中通过 `target_link_libraries(kf_sim PRIVATE ${LIBKUNGFU_NAME})`
复用主工程；扩展通过 `kungfu/wingchun/extension.h` 的工厂宏导出
`kf_create_trader` / `kf_create_market_data` 等 C 符号，宿主进程通过
`kungfu::wingchun::plugin::Library` 按 `libkf_sim.so` 的句柄解析。

---

## 启动方式

```bash
# 模拟行情源（md/sim/sim → 默认值）
setsid nohup ./kf_md --group sim --name sim --source sim \
  > /tmp/kf_md.out 2>&1 < /dev/null &

# 模拟交易柜台（td/sim/sim，默认 source=sim）
setsid nohup ./kf_td --group sim --name sim --source sim \
  > /tmp/kf_td.out 2>&1 < /dev/null &
```

`--group` / `--name` 决定 location uname `td/<group>/<name>/live` 以及
Config 行的主键；配置通过 kungfu 的 profile SQLite 写入后由 kf_cached
推送到对应 app（见下方“配置项”）。

---

## 配置项

配置是挂到 TD / MD 对应 location 上的一段 JSON，写入
`~/.config/kungfu/home/runtime/system/etc/kungfu/db/live/config.db`
的 `config` 表。TraderSim 与 MarketDataSim 在 `on_start()` 时通过
`get_config()` 读取；读取失败会使用硬编码默认值并打 WARN 日志。

### TraderSim 配置（TD 用）

```json
{
  "match_mode": "fill"
}
```

`match_mode` 可选值（字符串不区分大小写，对应 `src/match_mode.h`）：

| 值 | 行为 |
|---|---|
| `fill`（默认） | 下单立刻全量成交 |
| `pend` | 进入挂起，等待外部撤单或行情事件触发 |
| `cancel` | 先挂起再自动撤单 |
| `reject` | 直接返回 OrderStatus::Error |
| `partialfill` | 部分成交，留余挂单（PartiallyFilledActive） |
| `partialfillandcancel` | 部分成交后撤销剩余量 |
| `multiple_transactions` | 拆成多笔小成交逐步填满 |

### MarketDataSim 配置（MD 用）

```json
{
  "base": 200.0,
  "bound": 1000,
  "samples": 1000,
  "variation": 4,
  "randseed": 6
}
```

字段对应 `src/order_book.h` 中 `MakerConfig`：

- `base`：初始盘口中间价，`Subscribe` 到合约时以 `base±i×tick` 初始化 20 档
- `bound` / `variation` / `samples`：每 500ms 在 `[-variation, +variation]` 内
  随机 `samples` 笔小单扰动盘口，`bound` 作为价格边界（防止价格发散）
- `randseed`：随机种子，便于回归时复现固定行情序列

### 最小手数

`TraderSim::get_min_volume(InstrumentType::Stock) = 100`（股），其余品种为 1。
下单小于该数量会被直接拒绝（OrderStatus::Error）。

---

## 行为说明

### MarketDataSim

1. 订阅新合约时（事件流中的 `InstrumentKey`）自动构造 20 档初始盘口
2. 每 500ms 对所有已订阅合约扰动盘口，生成一笔新 `Quote` 写入 PUBLIC
   channel
3. `BrokerState` 在 `on_start` 时直接置为 `Ready`，无需登录/前置探测

### TraderSim

1. `on_start` 时读取 `match_mode`、调用 `enable_self_detect()`（自动侦测账户/
   持仓/库存字段，与 ledger 默认状态对齐）并置状态为 Ready
2. 对每个 `OrderInput`：
   - 拒绝逆规则单（Repo 买、数量不足等）→ OrderStatus::Error
   - 按 match_mode 构造 Order/Trade 事件，写入 source 对应策略的私有通道
     并同步到 PUBLIC 快照
3. 撤单 `OrderAction` 对非终态订单直接置为已撤并回写
4. 账户总资产随成交现金/持仓变动由 ledger 状态累积；sim broker 本身不维护
   独立的资金账户模型
5. 支持 `BatchOrderBegin`/`BatchOrderEnd`：同一批在一次事件循环内集中撮合

---

## 集成调试建议

1. **先零配置启动 sim**：TraderSim / MarketDataSim 都带默认值，没有
   Config 行也能起来
2. **用 kf_api 或 Qt 客户端订阅合约**：MD 收到 `InstrumentKey` 后才会生成
   行情并开始 quote 流
3. **需要确定性行情时把 `randseed` 固定**：相同 seed + 相同订阅序列可以
   复现逐笔波动，方便策略回归
4. **需要更严格的风控场景**：配置 `match_mode=pend` 后再用撤单/行情
   触发组合行为，接近"挂单撮合"的交易时序
