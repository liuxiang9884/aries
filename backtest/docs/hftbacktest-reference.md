# `hftbacktest` 参考实现调研

更新时间：2026-08-01

## 调研基线

- 上游仓库：`https://github.com/nkaz001/hftbacktest`
- 本地只读副本：`/home/liuxiang/dev/hftbacktest`
- 分支与 commit：`master` / `5f3ec40b2afb764e0fea112f941ed85523ef4e88`
- 上游版本：Rust crate `0.9.4`，Python package `2.4.4`
- 许可证：MIT
- 阅读范围：根文档、`docs/`、Rust core、Python / Numba binding、data utility、
  recorder / stats、collector、connector、live bot、examples、tests 与 roadmap

本文记录该 commit 的实现事实和可借鉴边界，不是 `aries` 回测 contract，也不代表
上游最新版本。后续设计应引用本文中的事实，但所有取舍仍需单独锁定和验证。

## 一句话概括

`hftbacktest` 是一个以完整逐笔行情回放为输入、同时维护 exchange view 与 local
view、通过订单请求 / 响应延迟连接两侧，并以可替换的 depth、queue、fill、fee 和
asset model 模拟订单生命周期的离散事件回测器。Rust 提供核心状态机，Python
通过 PyO3 与 Numba 暴露低开销研究接口；同一个 Rust `Bot` trait 也被 live bot
实现。

## 仓库结构

| 路径 | 职责 | 备注 |
|---|---|---|
| `hftbacktest/` | Rust 回测 core、market depth、live bot | 默认同时启用 `backtest` 与 `live` feature |
| `py-hftbacktest/` | PyO3 binding、Numba API、数据转换、统计与绘图 | `maturin` 构建；Python `>=3.11` |
| `hftbacktest-derive/` | Rust derive macro | 主要服务 NPY dtype 和 asset builder |
| `collector/` | 交易所 WebSocket 行情采集 CLI | 覆盖 Binance、Bybit、Hyperliquid 等实现 |
| `connector/` | 独立实盘 connector 与订单管理 | 通过 iceoryx2 shared-memory IPC 连接多个 bot |
| `docs/` | Sphinx 用户文档和 API 入口 | 重点覆盖 data、latency、fill、queue 与实盘差异 |
| `examples/`、`hftbacktest/examples/` | Python notebook、Python / Rust 策略示例 | 当前共有 24 个 notebook |

它不是单一 library：workspace 同时覆盖数据采集、标准化、回测、研究接口和有限的
实盘通路。不过这些部分的成熟度不同，上游 Rust README 明确说明仍可能发生
breaking change，live bot 尚未经过全面测试。

## 核心运行结构

每个回测资产由三部分组成：

- `local`：策略当时能够看到的 order book、trades、orders 与 account state。
- `exch`：exchange timestamp 上的真实市场回放和撮合状态。
- `reader`：供 local / exchange 两侧按各自时间读取的共享历史事件。

多个 `Asset` 被装配成一个 `Backtest`。策略不直接驱动内部 processor，而是调用
`Bot` 接口的 `elapse`、`wait_next_feed`、`submit_order`、`modify`、`cancel` 和
`wait_order_response`。核心数据流如下：

```text
historical Event --exch_ts--> Exchange Processor --fill/ack--> response bus
       |                            ^                         |
       |                            | entry latency           | response latency
       +--local_ts--> Local Processor --order request--------+
                           |
                           +--> strategy reads depth/orders/state
```

这种双 processor 结构的关键价值是：exchange 可以在策略尚未收到行情时继续演进，
订单也可以在 feed 与 order latency 共同作用下到达不同的 exchange state，从结构上
避免把 `signal time`、`order time`、`exchange processing time` 和 `response time`
压成一个时间点。

## 事件与时间模型

### 统一事件行

`Event` 使用 `#[repr(C, align(64))]` 的固定布局；Python 侧使用对应的 aligned NumPy
structured dtype。

| 字段 | 类型 | 语义 |
|---|---|---|
| `ev` | `u64` | event kind、side、exchange/local visibility 的 bit flags |
| `exch_ts` | `i64` | 事件在交易所发生的时间 |
| `local_ts` | `i64` | 本地收到事件的时间 |
| `px` | `f64` | 价格 |
| `qty` | `f64` | 数量 |
| `order_id` | `u64` | L3 Market-By-Order 的订单 ID |
| `ival` | `i64` | 预留整数 |
| `fval` | `f64` | 预留浮点数 |

event kind 包括 L2 depth update / clear / snapshot / BBO、trade，以及 L3 add / cancel /
modify / fill。`BUY_EVENT` / `SELL_EVENT` 的含义依上下文变化：depth 中表示 book side，
trade 中表示 aggressor side。`EXCH_EVENT` 与 `LOCAL_EVENT` 决定同一行是否分别由
exchange processor 和 local processor 处理。

时间单位没有在类型层强制；文档推荐 nanosecond，live bot 也使用 nanosecond。
Python data utility 提供：

- `correct_local_timestamp`：修正负 feed latency。
- `correct_event_order`：按 exchange / local 两条时间线拆分或标记事件。
- `validate_event_order`：分别检查有效 exchange events 的 `exch_ts` 和有效 local
  events 的 `local_ts` 单调性。

### 离散事件调度

每个资产在 `EventSet` 中有四类候选事件：`LocalData`、`LocalOrder`、`ExchData`、
`ExchOrder`。调度器每次选最小 timestamp，处理后只推进对应通道。

当前实现对相同 timestamp 的优先级是隐式的：数组按 asset 排列，每个 asset 内依次
为上述四类；比较只使用 `<`，因此低 asset number 和低 kind index 优先。这保证了
确定性，但 asset 顺序会参与同 timestamp 的结果。`aries` 不能直接继承这个隐式
规则，必须显式定义同 timestamp 的 feed、order request、exchange event、fill 和
response 优先级，并为 tie case 建测试。

## Order book 与行情视图

核心用 trait 拆分能力：

- `MarketDepth`：BBO、tick / lot size 和指定 price tick 的数量查询。
- `L1MarketDepth`：BBO update。
- `L2MarketDepth`：Market-By-Price update / clear。
- `L3MarketDepth`：Market-By-Order add / modify / delete / clear。
- `ApplySnapshot`：初始化和导出 snapshot。

现有实现包括：

- `HashMapMarketDepth`：支持 L2/L3，强调 missing feed 下的 natural refresh。
- `ROIVectorMarketDepth`：只保存指定 price range，以连续 vector 换取局部 order book
  计算效率；范围外事件仍可能影响使用边界，调用方必须正确配置 ROI。
- `BTreeMarketDepth`：有序 map 实现，主要提供 L3 能力；上游注释提示 L2 missing
  feed 可能留下错误 BBO。
- `FusedHashMapMarketDepth`：按 timestamp 融合不同频率或不同深度范围的数据流，
  并忽略过期更新。

价格在进入 depth 和 order 时通常通过 `round(price / tick_size)` 转成整数 tick，
数量有效性也按 lot size 判断。这个做法减少了价格比较中的浮点歧义，但原始价格、
tick rounding policy、动态 tick table 和 corporate / contract metadata 仍需由上层
contract 负责。

## 订单状态、延迟和请求通路

`Order` 同时保存目标数量、剩余数量、单次执行数量、委托 / 成交 price tick、
local / exchange timestamp、side、order type、TIF、request state、order status、
maker flag 和 queue-model 私有状态。

local 与 exchange 之间有两条 `OrderBus`：

1. local 发出 new / modify / cancel；`LatencyModel::entry` 计算 exchange receipt time。
2. exchange 处理后返回 ack / reject / fill；`LatencyModel::response` 计算 local receipt
   time。

内置 latency model 包括常量 latency 和基于历史 `(req_ts, exch_ts, resp_ts)` 的线性
插值。负 entry latency 被编码为到达 exchange 前的技术性 reject。跨地区研究可以
通过 offset 同时调整 feed latency 与 order latency。

`OrderBus::append` 会把新消息 timestamp clamp 到队尾 timestamp，从而强制请求和
响应 FIFO。源码明确承认现实中的 REST 请求可能乱序，但为简化回测不模拟这种情况。
这属于需要显式接受或替换的行为边界。

## Exchange、fill 与 queue model

### L2 exchange model

- `NoPartialFillExchange`：resting order 满足 crossing / trade / queue-front 条件后整笔
  成交；主动吃单不考虑 best quantity，直接在 best price 全部成交。
- `PartialFillExchange`：resting order 可按 trade remaining quantity 部分成交；主动
  订单会沿 replay depth 消耗可见数量并生成多个 fill，但不会修改随后回放的市场。
  当前 market order 搜索范围还存在固定 100 ticks 与 `TODO` 边界。

两者均为 market-data replay，不模拟自身订单对市场、后续 depth、其他参与者或
价格路径的影响。上游文档将“小到不足以造成 market impact”列为核心前提；因此
partial fill 不等于具备真实 impact model。

### Queue model

- `RiskAdverseQueueModel`：撤单 / 数量减少只发生在本单之后，queue position 仅由同价
  trade 推进，是保守 fill 假设。
- `ProbQueueModel`：根据 queue 前后数量的 power / logarithmic probability function
  分配 depth decrease，并扣除已观察 trade 以避免重复推进。
- `L3FIFOQueueModel`：将 market-feed order 和 backtest order 放入同价 FIFO queue，
  用于 Market-By-Order 回放。

modify 是否失去 queue priority 取决于 price 和 quantity 变化；源码中仍有
exchange-specific specialization 与 `Status::Replaced` / `Status::New` 的 TODO。
L3 当前只有 `L3NoPartialFillExchange`，不能把“支持 L3”理解为已经覆盖所有 L3
撮合和部分成交语义。

## 资产、费用、状态和指标

Rust core 提供：

- `LinearAsset` 与 `InverseAsset` 的 notional / equity 公式。
- 按 trading value、trading quantity、per trade 收费的 fee model。
- maker / taker rate，以及 Rust 侧按 buy / sell 方向叠加的 fee。
- 每资产 `position`、`balance`、`fee`、`num_trades`、`trading_volume`、
  `trading_value` 累计状态。

`BacktestRecorder` 按策略选择的采样点记录 mid price 与上述状态，Python `stats`
再使用 Polars resample / partition 并计算 Return、Sharpe、Sortino、MaxDrawdown、
turnover 等指标。

核心没有形成完整 portfolio ledger：未见现金可用量、资金占用、保证金、组合级净额、
风险限额、涨跌停、停牌、交易日历、公司行动或期货结算的统一执行 contract。这些不能
由简单 fee / asset model 代替，也是 `aries` 设计时必须独立处理的边界。

## 数据读取和性能路径

- core 直接读取 `.npy` / `.npz` 或接收内存 `Data<Event>`；多个文件由调用方按时间
  顺序提供。
- `Reader` 使用引用计数 cache 让 local / exchange 两个 reader 共享同一块数据，
  并可用后台线程预取下一文件；Parquet 仍在 roadmap。
- Rust hot path 使用静态 trait / builder 和部分 `unsafe` unchecked access；
  `EventSet` 使用 cache-line aligned array 扫描候选 timestamp。
- Python 使用 PyO3 构建 Rust 对象，再以 C-compatible 数据布局和 Numba jitclass
  让策略循环在 `@njit` 中执行，避免逐事件回到 Python interpreter。
- Rust `Bot` trait 同时由 `Backtest` 和 `LiveBot` 实现，使策略 API 可以复用；live
  侧通过独立 connector 和 iceoryx2 shared-memory IPC 隔离交易所连接。

这些实现体现了性能意识，但本文没有 benchmark 证据，不能据此宣称其吞吐、延迟或
内存效率适合 `aries` 的目标规模。

## 测试、文档与当前风险

在基线 commit 中静态统计到 27 个 Rust `#[test]` / `#[tokio::test]`、2 个 Python
test method 和 24 个 notebook。Rust tests 主要覆盖 depth、fused depth、部分 queue
逻辑和少量调度行为；Python test 依赖本地数据文件。GitHub Actions 中未见持续运行
`cargo test` / `pytest` 的常规 workflow，现有 workflow 主要是 Python CodeQL、
release build 和 stale issue 管理。

调研时还观察到以下代码 / 文档漂移：

- 当前 `pyproject.toml` 要求 Python `>=3.11`，但 `docs/index.rst` 仍写 `3.10+`。
- Rust `L2AssetBuilder` / `ReaderBuilder` 实际默认 `parallel_load = false`，相邻文档
  注释写默认 `true`；Python `BacktestAsset` 的默认值则是 `true`。
- Python `Order.tick_size` property 在当前源码返回 `price_tick`，不是 `tick_size`。

因此应借鉴其架构分解和模型接口，不应复制未重新验证的默认值、binding 行为或
统计口径。

### 本次验证边界

本机 `cargo 1.75.0` / `rustc 1.75.0` 无法解析上游要求的 Rust edition 2024 与
`rust-version = 1.91.1`，`cargo metadata --no-deps` 已在 manifest parse 阶段失败。
本次未安装新 toolchain，也未运行上游 build / tests；所有代码结论来自基线 commit
的静态阅读。进入实现选型前，如需验证上游行为，应在隔离环境使用 Rust 1.91.1
重跑 focused test 和自建 timestamp / fill fixtures。

## 对 `aries` 后续设计的输入

值得保留并在 Grill Me 中逐项决定的思想：

1. exchange time 与 local receive time 分离，订单 entry / response latency 独立建模。
2. local view 与 exchange view 分离，避免策略读取尚未收到的行情。
3. event scheduler、market depth、exchange、queue、latency、fee、asset accounting
   使用独立 contract，可通过小样本 fixture 替换验证。
4. 价格尽早转成整数 tick，数量按 lot size 校验；原始单位和 rounding rule 显式版本化。
5. 把 market replay 的 no-impact 前提写进实验结果，fill fidelity 按数据能力分级。
6. 研究 API 与执行 core 分层，但是否追求 backtest / live 共用策略 API 需要单独取舍。

不能直接继承的部分：

1. 通用 8 字段 `Event` 不能替代 `aries` 的正式数据 schema、instrument metadata、
   trading calendar 和数据版本 contract。
2. 相同 timestamp 的隐式 asset / event priority 必须改为显式、可测试的规则。
3. no-impact、FIFO order transport、L2 queue probability 和 modify priority 都是模型
   假设，必须与目标市场及可获得数据校准。
4. per-asset balance / position 不足以表达资金、保证金、组合风险和跨资产约束。
5. 上游 stats 只能作为展示参考；`aries` 的 gross / net、PnL、turnover、Sharpe、
   drawdown 和 attribution 必须建立自己的口径与 golden fixture。
6. NPY / NPZ、Numba 和 shared-memory live architecture 是实现选择，不是回测正确性的
   必要条件。

## 下一步阅读入口

后续 Grill Me 应以本文为事实基线，优先锁定：首个研究场景与资产范围、输入数据
能力、事件优先级、fill fidelity、latency / queue / cost 校准、资金与风险约束、策略
API、性能目标，以及从 deterministic fixture 到真实逐笔 replay 的验证阶梯。
