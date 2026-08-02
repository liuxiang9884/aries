# TWSE / TPEx 股票 Orderbook 字段设计

更新时间：2026-08-02

状态：**设计建议，尚未实现**。当前行为仍以 `data/converter/docs/twse.md` 记录的
Orion-compatible 23 列 orderbook CSV 为准。本文是后续股票专属 `Orderbook<N>`
schema review 的事实源，不描述当前已经发布的数据版本。

## 结论

TWSE / TPEx 与 TAIFEX 应继续使用各自 exchange namespace 下的独立
`Orderbook<N>`。股票与期货只统一命名、时间单位、价格类型和 writer 等基础约定，
不建立字段并集或公共数据基类。

股票现有 struct 仍需要调整：它混合了静态 basic-info、动态 actual 状态、试算揭示、
原始 bitmap 和 Orion legacy CSV 字段。建议将股票正式 Orderbook 收敛为：

- identity、交易日和事件时间；
- source message 与事件类型；
- 股票协议特有的 typed disclosure / limit / session flags；
- actual OHLC、成交累计状态；
- 最近一次完整揭示的普通五档价格和数量。

reference price、price limit、multiplier 和 currency 继续以 basic-info 为事实源；精确
成交进入独立 `Trade`；可推导因子不写入 Orderbook。

## 范围与事实源

本设计交叉核对：

- `data/docs/exchange/TWSE集中市場即時交易資訊傳輸規格書(B.12.11)(202503)_20250113092444.pdf`
- `data/docs/exchange/上櫃股票IP行情網路規格書(V.12.16 TCPIP).pdf`
- Aries 当前 TWSE / TPEx protocol、basic-info、decoder、Orderbook 和 CSV writer
- `/home/liuxiang/dev/orion` 的 `StockDepth<5>`、TWSE data converter、data engine、
  data reader 和 to-csv writer
- 2026-07-07 stock dump 与当前完整转换验证

本文只设计 stock dump converter 的日盘研究记录，不扩展当前处理的 format 范围，
也不实施 schema 或历史重建。

## 设计原则

1. 同名字段必须具有稳定业务语义和明确单位。
2. actual 累计状态不得被 trial、暂缓撮合或快照采样污染。
3. Orderbook 表示最近一次完整揭示的普通五档状态；source 未携带 book 时必须显式标记，
   不能把“未揭示”误写成“空盘口”。
4. 每笔成交、日级 reference、证券状态、市场统计和研究衍生字段分开建模。
5. 交易所原始 flag 应无损保存，但业务代码不直接解释一个 packed `int64_t status`。
6. offline dump 没有真实 receive timestamp，不得用 fallback `local_ns` 研究延迟。
7. 所有价格使用 `double`；数量使用有符号 `int64_t`，并显式记录 source-native 单位。

## 当前 struct

当前 `aries::data::twse::Orderbook<N>` 包含：

```text
symbol
exchtime,localtime,status
last_price,previous_close,open,high_limit,low_limit,multiplier
total_volume,total_value,total_trade
ask_price[N],ask_volume[N],bid_price[N],bid_volume[N]
sequence
```

它同时承担 decoder 的 per-symbol mutable state 和 CSV row，导致静态 metadata、原始
source flag、actual 状态和本次消息 payload 混在一个对象中。后续实施应由内部
`StockState` 组合 basic-info/reference state 与要发布的 `Orderbook<N>`，不再让
Orderbook 负责保存所有 builder metadata。

## 当前字段逐一建议

| 当前字段 | 建议 | 目标语义 / 原因 |
|---|---|---|
| `symbol` | 保留 | 去除尾端空白的交易所代码；稳定身份是 `(trading_day, market, symbol)` |
| `exchtime` | 改名 `exchange_ns` | 触发该行的交易所业务事件时间，Unix epoch nanoseconds |
| `localtime` | 改名 `local_ns` | 真实接收/采集时间；offline 暂用 exchange fallback，但不得用于 latency |
| `status` | 删除并拆分 | 当前是三个不同 bitmap 的 packed integer；改用 typed `disclosure`、`limit_state`、`session_state` |
| `last_price` | 保留并收紧 | 截至本行最近一笔 **actual** 成交价；trial / held 揭示不得覆盖 |
| `previous_close` | 改名 `reference_price` 并移出 | 今日参考价属于日级 reference/basic-info，不一定等于未经调整的昨日收盘价 |
| `open` | 保留并收紧 | 当日首笔 actual 成交价；trial 不更新 |
| `high_limit` | 移出 | basic-info / reference state 的日级价格限制，不在每行重复 |
| `low_limit` | 移出 | 同 `high_limit` |
| `multiplier` | 移出 Orderbook，builder 保留 | basic-info 是事实源；format 1 与 format 22 都应解析，计算 value 和解释数量单位时使用 |
| `total_volume` | 保留并收紧 | actual 累计成交量；单位由 source contract 决定 |
| `total_value` | 保留并收紧 | converter 推导的 actual 非负累计成交额，单位为 basic-info currency |
| `total_trade` | 改名 `trade_volume` | 当前实际是本次揭示成交量，不是累计成交笔数；正式语义只统计本行 actual 成交量 |
| `ask_price[N]` | 保留 | 最近一次完整揭示的普通卖方第 1..N 档价格 |
| `ask_volume[N]` | 保留并持久化 | 交易所直接提供；当前 struct 已有、legacy 23 列 CSV 丢弃 |
| `bid_price[N]` | 保留 | 最近一次完整揭示的普通买方第 1..N 档价格 |
| `bid_volume[N]` | 保留并持久化 | 交易所直接提供；当前 struct 已有、legacy 23 列 CSV 丢弃 |
| `sequence` | 改名 `source_sequence`，类型用 `uint64_t` | TWSE header sequence 每日、每 market、每 format 独立，必须由 `source_message` 限定 scope |

### 数量与 multiplier

本设计延续此前“保留交易所原生数量”的决定，不增加 `unit_count`：

- format 6 / 17 的 trade、bid、ask 与累计 volume 单位均为**交易单位**；实际证券数量为
  `volume * multiplier`。
- format 23 的 quantity 已是**股数**，计算 value 时有效 multiplier 为 1；format 22
  提供的 multiplier 仍表示该证券的标准交易单位大小，不能乘到零股 quantity 上。
- 同一输出 generation 不应混合 regular 与 odd-lot source。字段单位必须由
  `source_message`、运行 mode 和数据 manifest 共同锁定。
- `total_value`：regular 为 `price * trading_unit_volume * multiplier`；odd-lot 为
  `price * shares`。

当前 `ProcessOddLotBasicInfo()` 只读取 reference/high-limit/low-limit，尚未读取 format 22
的 multiplier；后续实施必须补齐并用 fixture 锁定 offset 和单位。

## 建议新增字段

| 字段 | C++ 类型 | 单位 / 取值 | 建议语义 |
|---|---|---|---|
| `trading_day` | `int32_t` | `YYYYMMDD` | converter 分区交易日 |
| `market` | `Market` | `TWSE` / `TPEX` | header service type 对应的实际市场 |
| `source_message` | `SourceMessage` | `format6` / `format17` / `format23` | 直接触发本行的 wire format；限定 sequence 和数量单位 |
| `event_type` | `EventType` | `trade` / `orderbook` / `trade_orderbook` / `status` | 本消息是否携带 actual/trial trade、是否更新普通 book |
| `high` | `double` | 价格 | 截至本行的 actual 当日最高成交价 |
| `low` | `double` | 价格 | 截至本行的 actual 当日最低成交价 |
| `disclosure` | typed 1-byte wrapper | wire bitmap | 成交 payload、揭示档数和 trade-only/full-book 语义 |
| `limit_state` | typed 1-byte wrapper | wire bitmap | 成交/best bid/best ask 涨跌停与暂缓撮合趋势 |
| `session_state` | typed 1-byte wrapper | wire bitmap | trial、延后开收盘、撮合方式和开收盘标记 |

`event_type` 是稳定研究语义，typed flag 保留协议细节。两者可以由 decoder 同时生成，
不要求下游反复解释 raw bits。

## 股票特有 typed flags

typed wrapper 应保存原始 `uint8_t`，并通过 enum / named accessor 暴露语义；不要依赖
C++ bitfield ABI。

### `DisclosureState`

format 6 / 17 / 23 的 disclosure byte：

| bit | 语义 |
|---|---|
| 7 | 是否携带成交价量 payload |
| 6-4 | bid 揭示档数，允许 0..5 |
| 3-1 | ask 揭示档数，允许 0..5 |
| 0 | format 6/17：`0` 为本消息同时揭示最终五档，`1` 为 trade-only；format 23 为保留 |

format 6/17 的 bit 0 是关键字段：同一 incoming order 可能产生多条相同撮合时间的成交
揭示，只有最后一条附带撮合完成后的五档。trade-only 行应沿用上一份**已知**完整 book，
并用 `event_type=trade` 表示本行没有更新 book；这份 book 是 last-known state，不是假装成
该中间成交后的真实盘口。

format 23 不提供 trade-only bit。当前设计把每条 format 23 视为完整零股盘口揭示；正式
实现前仍需用真实 odd-lot dump 验证空盘口、仅成交和零档边界。

### `LimitState`

limit byte：

| bit | 建议 enum | 值 |
|---|---|---|
| 7-6 | `trade_limit` | normal / down-limit / up-limit / reserved |
| 5-4 | `best_bid_limit` | normal / down-limit / up-limit / reserved |
| 3-2 | `best_ask_limit` | normal / down-limit / up-limit / reserved |
| 1-0 | `instantaneous_trend` | normal / held-down / held-up / reserved |

held-down / held-up 消息中的“成交价量 payload”实际是最近成交价和零成交量，撮合时间是
暂缓撮合起始时间；它是 status event，不得生成 actual Trade，也不得更新 actual OHLC、
volume 或 value。

### `SessionState`

status byte：

| bit | 建议字段 | 值 / 边界 |
|---|---|---|
| 7 | `is_trial` | `0=actual`、`1=trial` |
| 6 | `is_delayed_open` | format 6/17 且 trial 时有效；format 23 保留 |
| 5 | `is_delayed_close` | format 6/17 且 trial 时有效；format 23 保留 |
| 4 | `matching_method` | call-auction / continuous |
| 3 | `is_opening` | 开盘揭示 |
| 2 | `is_closing` | 收盘揭示 |
| 1-0 | reserved | 必须验证为协议允许值，不转成业务字段 |

CSV 若需要扁平化，应至少直接输出 `is_trial` 和 `event_type`；其余股票特有状态可以扁平
输出或进入同 generation 的 stock status 数据集，但不能继续只输出 packed decimal
`status`。

## 建议的股票 `Orderbook<N>`

最终 logical fields 建议为：

```text
trading_day
market
symbol
exchange_ns
local_ns
source_message
event_type
source_sequence

disclosure
limit_state
session_state

open
high
low
last_price
trade_volume
total_volume
total_value

ask_price[N]
ask_volume[N]
bid_price[N]
bid_volume[N]
```

### 时间 contract

- `exchange_ns` 使用 format 6/17/23 body 的撮合时间，不使用 dump 读取时间。
- 协议只有 microsecond 精度，转换为 ns 后低三位为 0。
- `local_ns` 正式定义为真实接收/采集时间。当前 raw dump 没有该字段，offline 暂令
  `local_ns = exchange_ns`，同时在 manifest/log 写
  `local_time_source=exchange_fallback`。
- fallback `local_ns` 不得参与延迟、排队或基础设施性能研究。

### actual / trial contract

- `open/high/low/last_price/trade_volume/total_volume/total_value` 只由 actual trade 更新。
- trial price/volume 写入 `Trade(is_trial=1)`，但 actual 累计状态保持不变。
- `trade_volume` 是触发当前 Orderbook 行的 actual 成交量；没有 actual 成交时为 0。
- `total_volume` 使用交易所累计字段，不得通过 sum row trade volume 代替，因为局部 gap
  或恢复后两者可能不同。
- `total_value` 由 converter 逐 actual 成交计算。frame gap 无法恢复遗漏成交的精确价格
  路径，因此受影响 symbol 继续遵守当前 invalidation / 日级质量日志 contract。

### book contract

- 空 level：`price=0, volume=0`。
- 市价委托：交易所直接使用 `price=0, volume>0`；不能仅用 price 判断空档。
- full-book 消息用 source 揭示内容替换普通五档，未使用尾档清零。
- format 6/17 trade-only 消息不清空五档，沿用上一份 last-known full book，并由
  `event_type` 表示 book 未更新。
- format 23 的数量是股数；regular format 6/17 的数量是交易单位。

## Static reference / builder state

以下字段不进入每条正式 Orderbook，但 decoder/builder 必须按
`(trading_day, market, symbol)` 持有，并以 basic-info CSV 为持久化事实源：

```text
reference_price
high_limit
low_limit
multiplier
currency
```

`StockState` 可以组合这组 reference state、actual trade state、last-known book 和
source sequence validation state；这不意味着它们都应写入 Orderbook CSV。

## 独立 `Trade`

TWSE / TPEx format 6/17/23 每个消息最多携带一项成交或试算价量，但同一撮合时间可连续
出现多条 trade-only，最后一条才附完整五档。只保留 Orderbook 无法清楚表达 trial 与
逐次成交路径，正式 schema 应同步输出 `Trade`：

```text
trading_day,market,symbol,exchange_ns,local_ns,is_trial,
price,volume,total_volume,total_value,
source_message,source_sequence,sequence
```

- normal actual payload 生成 `Trade(is_trial=0)` 并更新 actual cumulative state。
- trial payload 生成 `Trade(is_trial=1)`，actual total volume/value 保持不变。
- held-down / held-up 的“最近成交价、volume=0”不生成 Trade，写成 status event。
- `sequence` 是每 `(trading_day, market, symbol)` 的 Trade row ordinal；
  `source_sequence` 保留交易所 format sequence。
- aggressor side、order id、queue position 和 per-level order count 未由协议提供，不得
  通过猜测写成 exchange fact。

## high / low 与 snapshot 字段

股票 Orderbook 应增加 actual `high/low`，但不能通过未来统计回填早期行：

- format 6/17/23 的 normal actual Trade 实时更新 OHLC。
- TWSE format 12 / TPEx format 11 提供 symbol-level open/high/low/last/total volume，
  用作按消息时间和收盘结果的一致性校验；若后到消息与已构建状态不一致，按日期、市场、
  symbol 和 source sequence 记录问题，不静默覆盖早期状态。
- format 20/24 是五秒 snapshot，包含 open/high/low/last/total volume/五档，但文档明确
  最近成交价量只是瞬间采样，同一撮合时间不保证是最后一笔。
- 2026-07-07 dump 有 TWSE format 12 与 TPEx format 11，但没有 format 20/24；后者只能
  作为协议能力和未来 recovery/validation source，不能声称当前采集链已提供。
- snapshot 未来可单独输出或用于 recovery/validation，不能无标记混入 format 6/17 的
  逐次事件流。

## 不进入 Orderbook 的字段

| 字段 / 信息 | 处理 |
|---|---|
| `symbol_id` | runtime-local、offline 恒为 `-1`，不持久化 |
| `average_ask_price` / `average_bid_price` | 可由五档价格量计算；市价档会影响定义，研究层按需计算 |
| `total_ask_volume` / `total_bid_volume` | 可由五档 volume 求和，研究层按需计算 |
| mid、spread、imbalance、microprice | 因子层计算，不作为 source data |
| `delta_total_volume` / `delta_total_value` | 正常连续行可推导；gap 后必须结合日志解释 |
| board、security type、currency、warrant fields | basic-info |
| format 19 暂停/恢复、其他 lifecycle 状态 | 独立 `InstrumentStatus` |
| format 2/4 市场/类别统计 | market statistics，不伪造成 per-symbol 字段 |
| quality/recovery flag | 日级 manifest/log；当前受损 symbol 直接 invalidated |
| aggressor side | 协议未提供；tick rule/book inference 只能作为研究推断 |
| order count、order id、queue position | 协议未提供，禁止伪造 |

## 相对当前实现必须修正的行为

后续 schema 实施除了改字段，还必须修正以下状态语义：

1. 当前 trade-only 消息的 bid/ask level count 为 0，decoder 随后清空全部数组；新 contract
   必须保留上一份 last-known full book，并通过 `event_type` 标记未更新。
2. 当前 `is_traded` payload 无条件覆盖 `last_price`，trial 也可能影响 `open`；新 contract
   必须隔离 actual 与 trial state。
3. 当前 `total_trade` 同时承载 actual、trial 或 held payload volume，且名称暗示累计笔数；
   必须改为有严格 actual 语义的 `trade_volume`，逐笔原文进入 `Trade`。
4. 当前 format 22 multiplier 没有进入 builder state；必须解析并测试，但 odd-lot value
   仍使用有效 multiplier 1。
5. 当前 Orderbook struct 已有 bid/ask volume，legacy 23 列 CSV 却不输出；正式 schema
   必须持久化全部 N 档 price/volume。
6. 当前 `localtime=exchtime` 只是 compatibility fallback；新 manifest 必须显式记录来源。

## 与 Orion 的有意差异

- 不保留 `symbol_id=-1` 或 packed decimal `status` 作为正式研究字段。
- `total_trade` 更名并修正为 actual `trade_volume`，不沿用 Orion 的错误注释。
- actual last/OHLC/volume/value 不被 trial 或 held message 污染。
- 股票 CSV 首次完整输出 bid/ask volume。
- static reference 不在每条 Orderbook 重复，通过同 generation basic-info join。
- trade-only 不伪装为空盘口；last-known book 与消息更新类型同时保留。
- format 11/12 与未来 snapshot 只用于验证/recovery 或独立输出，不混入逐次主流。

## 后续实施顺序

该设计经用户 review 后，实施应作为独立 L3 数据 contract 迁移：

1. 先锁定 `Market`、`SourceMessage`、`EventType`、typed flags、stock
   `Orderbook<5>` 与 `Trade` fixtures。
2. 建立 trial/actual、held、trade-only/final-book、market order、empty book、regular/
   odd-lot quantity unit 的失败测试。
3. 分离 `StockState` 与 emitted records，修正 format 6/17/23 状态转换和 format 22
   multiplier。
4. 增加 format 11/12 decoder 作为 OHLC/last/total volume validation source。
5. 建立新 CSV schema/version 与 generation manifest，不兼容双写、不覆盖旧文件。
6. 跑 focused CTest、完整 CTest、一天真实 TWSE/TPEx full conversion 和独立全文件校验。
7. 测量新增 volume/Trade 后的 bytes、吞吐和压缩比，再决定长期存储格式。
8. 验证通过后才制定 `/tw_backup` 历史整批重建计划。

## 验收边界

- 每个字段具有唯一名称、单位、source 和 actual/trial 规则。
- market order 与 empty level 可由 `(price, volume)` 无歧义区分。
- trade-only 行不会被误认为空盘口，full empty book 仍能正确清空。
- actual OHLC/volume/value 在 trial 与 held 行保持不变。
- format 6/17 regular 与 format 23 odd-lot quantity/value 单位有 fixture。
- format 11/12 一致性问题按 date/market/symbol/source sequence 输出，不回填未来资料。
- 新旧 CSV 由 schema/version/manifest 隔离，旧新历史数据不得混用。

## 当前未实现边界

- 当前 23 列 CSV、struct、文件名和批量重建结果均未因本文改变。
- 2026-07-07 真实 dump 没有 format 17/23/20/24；warrant、odd-lot 与 snapshot 的真实
  数据回归仍需其他日期或采集源。
- 当前仓库尚未建立 generation manifest/schema version；`local_ns` fallback 仍只在文档
  层说明。
- 本文不设计 TAIFEX Orderbook 字段；期货专属 review 另立事实源。
