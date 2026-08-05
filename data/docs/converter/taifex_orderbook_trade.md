# TAIFEX Futures Orderbook / Trade 字段设计

更新时间：2026-08-02

状态：**设计建议，尚未实现**。当前转换行为仍以
`data/docs/converter/taifex.md` 记录的 44 列 orderbook CSV 与 27 列 basic-info
CSV 为准。本文是后续 TAIFEX futures 专属 `Orderbook<N>` 与 `Trade` schema review
的事实源，不描述当前已发布的数据版本。

## 结论

TAIFEX 与 TWSE / TPEx 应继续使用 exchange namespace 下的独立 record。二者统一
`exchange_ns` / `local_ns`、价格 `double`、数量 `int64_t`、Nova + Quill writer 和
manifest 规则，但不建立字段并集或公共数据基类。

TAIFEX 当前 struct 仍需要拆分。它把普通五档、衍生一档、actual 累计成交状态、trial
状态、I024 packet summary、I081 action、static reference 和 recovery quality 混在一行。
建议收敛为：

- `Orderbook<N>`：普通 N 档 actual 或 trial book，以及截至该行的 actual 成交状态；
- `Trade`：I024 每一项成交/试撮价量，一项一行；
- `DerivedOrderbook<1>`：TAIFEX 衍生一档；
- `TradeStats`：I024 packet-level 累计成交笔数与 exchange summary；
- 按需增加 `OrderbookDelta`、`OrderStats`、`HighLow` 和 session/daily statistics。

其中 `Trade` 必须同时保留 I024 `MATCH-TIME` 与 header `INFORMATION-TIME`，以及
first/continuation packet bit。仅保留当前 orderbook 行无法恢复一包多笔成交的价格路径。

## 范围与事实源

本设计交叉核对：

- `data/docs/exchange/期貨逐筆行情資訊傳輸作業手冊(V1.5.1).pdf`
- Aries 当前 TAIFEX protocol、decoder、recovery builder、records 与 CSV writer
- `/home/liuxiang/dev/orion` 的 `FutureOrderbook<5>`、TAIFEX converter、data engine、
  data reader、orderbook builder 与 `FactorCsvWriter`
- 2026-07-07 TAIFEX futures dump、当前完整 dry-run 和前 2,000,000 frame 的字段扫描

本文只设计当前 futures 日盘研究数据。夜盘、options、block trade 逐笔与跨 session
统计继续不在本轮范围内；也不实施 schema 或历史重建。

## 设计原则

1. 普通 book、derived book、actual state 与 trial state 分开维护，不能互相覆盖。
2. I024 一项 price/quantity 就是一条 Trade；packet summary 不能代替逐项路径。
3. 同一字段只表达一种时间、单位和累计口径；transport time 与 business event time
   不得混用。
4. exchange direct fact、converter cumulative state 与研究推导字段明确分层。
5. calendar spread 的负价和零价都是合法价格，不能用 `price == 0` 判断缺失或市价。
6. snapshot 可以恢复 book 和部分 exchange summary，不能伪造遗漏 Trade 或精确 value。
7. offline dump 没有真实 receive timestamp，fallback `local_ns` 不得用于 latency 研究。
8. 所有价格使用 `double`；quantity/volume/count 使用有符号 `int64_t`，volume 单位为
   contract。

## 当前 struct 与 CSV

当前 `aries::data::taifex::Orderbook<N>` 包含：

```text
trading_day,symbol
exchtime,localtime
reference_price
open,high,low,last_price
trade_volume,total_volume,total_value
total_buy_count,total_sell_count
ask_price[N],ask_volume[N],bid_price[N],bid_volume[N]
derived_ask_price,derived_ask_volume,derived_bid_price,derived_bid_volume
match_flag,build_type,orderbook_action
sequence
```

当前 CSV 另加常量 `market=TAIFEX` 与 `symbol_id=-1`，固定为 44 列。一行只由 I081 或
I083 触发；I024/I025 先修改 mutable state，再合并到下一条 book row。

这个结构无法无损表达：

- I024 的 `MATCH-TIME`、一包多项成交与 continuation；
- I081 内逐 entry 的 New / Change / Delete / Overlay 顺序；
- actual 与 trial 各自的普通 book；
- ordinary 与 derived book 的独立更新节奏；
- I025 `SHOW-TIME`；
- gap 后哪些累计 value 已不能精确恢复。

## 当前字段逐一建议

| 当前字段 | 建议 | 目标语义 / 原因 |
|---|---|---|
| `trading_day` | 保留 | converter 分区交易日，当前仅日盘 `YYYYMMDD` |
| `market`（仅 CSV） | 放入 record 并保留 | typed constant `TAIFEX`；稳定身份是 `(trading_day, market, symbol)` |
| `symbol` | 保留 | 去除尾端空白的 `PROD-ID`；outright 与 calendar spread 均原样保留 |
| `symbol_id`（仅 CSV） | 删除 | runtime-local；offline 恒为 `-1`，不是持久化身份 |
| `exchtime` | 改名 `exchange_ns` | 对 Orderbook 为触发行的 I081/I083 header `INFORMATION-TIME` |
| `localtime` | 改名 `local_ns` | 真实接收/采集时间；offline 只能使用显式 fallback |
| `reference_price` | 移出 | I010 日级 metadata；spread 可能无 I010，应从 basic-info join |
| `open` | 保留并修正 | actual session open；不能一律使用首个 actual I024 的第一项 |
| `high` | 保留并修正 | actual 当盘 high，由 Trade 更新并用 I025/I084 S/收盘统计校验 |
| `low` | 保留并修正 | actual 当盘 low，规则同 `high` |
| `last_price` | 保留并收紧 | 截至本行最近一项 **actual** Trade；trial 不得覆盖 |
| `trade_volume` | 保留并收紧 | 自上一条 actual ordinary Orderbook row 后观察到的 actual contract 数 |
| `total_volume` | 保留并收紧 | 最新 actual I024 `MATCH-TOTAL-QTY` 或有效 recovery summary |
| `total_value` | 保留并收紧 | observed actual 非负累计名义 value：`abs(price) * volume * multiplier` |
| `total_buy_count` | 移出 | I024 累计买进成交笔数，不是盘口字段；进入 `TradeStats` |
| `total_sell_count` | 移出 | I024 累计卖出成交笔数，不是盘口字段；进入 `TradeStats` |
| `ask_price[N]` | 保留 | 普通卖方第 1..N 档；signed `double` |
| `ask_volume[N]` | 保留 | 普通卖方第 1..N 档 contract 数 |
| `bid_price[N]` | 保留 | 普通买方第 1..N 档；signed `double` |
| `bid_volume[N]` | 保留 | 普通买方第 1..N 档 contract 数 |
| `derived_ask_price/volume` | 移出 | TAIFEX 衍生一档，进入 `DerivedOrderbook<1>` |
| `derived_bid_price/volume` | 移出 | 同上 |
| `match_flag` | 改名 `is_trial` | 对 Orderbook 只表示 I083 `CALCULATED-FLAG`；I081 恒为 actual |
| `build_type` | 删除并改为 `source_message` | `0/3` 只是 I081/I083 的不透明 legacy code |
| `orderbook_action` | 从主表删除 | 当前 0/1 压缩会丢 Change/Delete/Overlay 与 entry 顺序；需要时写 `OrderbookDelta` |
| `sequence` | 改名 `source_sequence` | TAIFEX per-product `PROD-MSG-SEQ`，跨 I024/I025/I081/I083 连续 |

### 不从 Orion 补回的字段

Orion `FutureOrderbook<5>` 还含 `channel_id`、packed `status`、`open_interest`、
`total_trade`、`total_ask_volume`、`total_bid_volume` 和 `continuous_flag`。它们不应补进
正式 Orderbook：

- `channel_id` / `CHANNEL-SEQ` 是 transport audit 信息，进入转换日志或 generation
  manifest；per-product `PROD-MSG-SEQ` 才是 symbol event continuity key。
- `open_interest` 来自 I072 收盘/session statistics，不是盘中 book state。
- `total_trade` 在 Orion 没有可靠、唯一的填充 contract。
- Orion 的 `total_ask_volume/total_bid_volume` 实际写入 I024 累计卖/买成交笔数，名称错误。
- `continuous_flag` 是 recovery quality；当前项目继续按 date/symbol/gap 写日志，不复制到
  每条高频行。

## 建议的 TAIFEX `Orderbook<N>`

最终 logical fields 建议为：

```text
trading_day
market
symbol
exchange_ns
local_ns
source_message
is_trial
source_sequence

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
best_ask_is_market
best_bid_is_market
```

TAIFEX 不需要复制股票的 `disclosure`、`limit_state`、`session_state` 或 `event_type`。
`source_message` 使用 typed `i081` / `i083`；消息类型本身已经区分
incremental/snapshot，`is_trial` 足以区分 trial snapshot。这是保留 exchange-specific
struct 的主要原因之一。

### 时间 contract

- Orderbook `exchange_ns` 使用触发行 I081/I083 common header 的
  `INFORMATION-TIME`，转换为 UTC+8 trading-day local clock 对应的 Unix epoch ns。
- I081/I083 没有比 header 更具体的业务时间，因此不另加 `information_ns`。
- `local_ns` 正式定义为真实 receive/capture time。当前 dump 没有该字段，offline 暂令
  `local_ns = exchange_ns`，并在 manifest 写
  `local_time_source=information_time_fallback`。
- fallback `local_ns` 不得用于 exchange latency、network latency 或 queueing 研究。
- 当前 contract 只处理 `13:46:00` 前日盘；夜盘日期映射仍 deferred。

### actual / trial state contract

builder 内部至少维护两份普通 book state：

- actual book：由 I081 与 `CALCULATED-FLAG=0` 的 I083 更新；
- trial book：只由 `CALCULATED-FLAG=1` 的 I083 完整替换。

trial I083 必须先清空并替换 **trial book**，不能清空 actual book；随后 actual I081 也不能
在 trial book 上继续做差量更新。两份 book 共用同一套 per-product source sequence
validation，但不共用 levels。发布 trial row 时 `is_trial=1`，五档来自 trial book，
`open/high/low/last_price/total_volume/total_value` 仍是截至该时刻的 actual state，
`trade_volume=0`。

actual I024 才能更新 `last_price`、OHLC、volume 与 value。trial I024 进入
`Trade(is_trial=1)`，不更新 actual state。`trade_volume` 只在 actual ordinary Orderbook
row 发布后归零；trial I083 或 derived-only 更新既不显示也不消费这段 pending actual
volume。

### 零价 spread、empty level 与 trial 市价单

TAIFEX calendar spread 可以合法成交或挂单在零价；同时 I083 规定 trial 最佳市价买单用
`+999999999`、trial 最佳市价卖单用 `-999999999`。因此不能复用股票的
`price=0, volume>0` 规则，也不能只用 price 判断 empty。

建议在 decoder 中把 trial market sentinel 规范化为：

| 状态 | price | volume | `best_*_is_market` |
|---|---:|---:|---:|
| empty | `0` | `0` | `0` |
| 合法零价 limit spread | `0` | `>0` | `0` |
| trial market order | `0` | `>0` | `1` |
| 普通 limit | source price | `>0` | `0` |

protocol 只允许 best level 使用该 market sentinel，因此两个 side-level boolean 足够；
不需要为全部 N 档重复 price type。若实现选择保留 sentinel 原值，则必须在 schema 明确
它不是可直接进入 mid/spread 的正常价格；相比之下，typed boolean 更适合研究使用。

### I081 / I083 发布规则

- I081 的 entries 必须按 source 顺序逐一应用；同一 message 的后一个 level 基于前一个
  更新后的 book。
- 一条 I081 含普通 action 时，完成全部 entries 后最多发布一条 ordinary Orderbook。
- derived-only I081 不发布重复 ordinary Orderbook，只发布 `DerivedOrderbook<1>`；精确
  Trade 已由独立表提供，因此不需要借 duplicate book row 承载成交状态。
- actual I083 先清空并完整替换 actual ordinary/derived state。即使 count=0，也必须发布
  ordinary empty-book row，并发布 derived empty state 以表示清空。
- trial I083 只替换并发布 trial ordinary book；协议说明 trial 阶段没有 derived order，
  不能用它清空 actual derived state。
- I084 O 只恢复 builder state 与 `source_sequence`，不伪装成一条实时 I083 market event；
  snapshot 之后 replay 的 I081/I083 保留其原始 source time/sequence。

## 独立 `Trade`

TAIFEX I024 每个 packet 含一项 `FIRST-MATCH-PRICE/QTY`，随后可含 0..70 项
`MATCH-PRICE/QTY`。同一 incoming order 若产生 100 笔成交，会拆成 first packet 与
continuation packet；两包 `MATCH-TIME` 相同。当前把整包压进下一条 Orderbook 会永久
丢失逐项价格路径，正式 schema 必须同步输出 `Trade`。

建议一项 price/quantity 对应一行：

```text
trading_day
market
symbol
exchange_ns
information_ns
local_ns
is_trial
price
volume
total_volume
total_value
is_first_packet
source_sequence
source_index
sequence
```

### Trade 字段 contract

| 字段 | 类型 / 单位 | 语义 |
|---|---|---|
| `trading_day` | `int32_t YYYYMMDD` | 当前日盘 converter partition |
| `market` | `Market::TAIFEX` | exchange identity |
| `symbol` | string | I024 `PROD-ID`，包含 outright 或 spread 原文 |
| `exchange_ns` | `int64_t ns` | I024 `MATCH-TIME`，即成交/试撮业务时间 |
| `information_ns` | `int64_t ns` | common header `INFORMATION-TIME`，即 packet exchange publish time |
| `local_ns` | `int64_t ns` | receive/capture time；offline 暂用 `information_ns` fallback |
| `is_trial` | bool | I024 `CALCULATED-FLAG`，`0=actual`、`1=trial` |
| `price` | signed `double` | 当前 item price；spread 可为负或零 |
| `volume` | `int64_t` contracts | 当前 item quantity，不合并不同 item |
| `total_volume` | `int64_t` contracts | 应用当前 actual item 后的 actual 累计成交量；trial row 保持 actual state |
| `total_value` | `double` currency | 应用当前 actual item 后的 observed actual 累计名义 value；trial row 保持不变 |
| `is_first_packet` | bool | I024 bitmap bit 7；表示本 packet 开始一个 incoming-order match group |
| `source_sequence` | `uint64_t` | packet 的 `PROD-MSG-SEQ`；同 packet 所有 item 相同 |
| `source_index` | `uint8_t` | packet 内 item index，FIRST 为 0，后续项为 1..70 |
| `sequence` | `uint64_t` | 每 `(trading_day,market,symbol)` 的 Trade row ordinal |

`(trading_day, market, symbol, source_sequence, source_index)` 是 source-level 唯一键；
`sequence` 是便于下游排序和窗口计算的 converter row key。`source_sequence` 不是 Trade-only
流水号，它与 I025/I081/I083 跨消息类型共用，出现数值跳号不等于 Trade 丢失。

TAIFEX 这张 Trade 表的唯一 source 是 I024，因此不为每行重复常量 `source_message`；
schema/version 与 generation manifest 必须锁定 `source=I024`。未来若接入不同 contract 的
block trade 或其他成交来源，应另建 record/file，不能无标记混入。

### MATCH-TIME 与 INFORMATION-TIME

两个时间都需要保存：

- `MATCH-TIME` 回答“成交何时发生”，用于逐笔排序、bar、label 和 market-state 对齐；
- `INFORMATION-TIME` 回答“交易所何时发布这个 packet”，用于 source audit；
- 二者都不是本机 receive time，`information_ns - exchange_ns` 也不能冒充 network latency。

2026-07-07 前 2,000,000 frame 样本中，127,339 个 I024 packet 有 91,114 个
`MATCH-TIME != INFORMATION-TIME`。继续把 header time 当作 Trade event time 会系统性改变
逐笔时间与对齐结果。

### multi-item 与 continuation

- 每个 packet 先输出 `source_index=0` 的 FIRST item，再按 wire 顺序输出 1..70。
- 同一 packet 的 Trade 共享 `exchange_ns`、`information_ns`、`is_trial`、
  `is_first_packet` 与 `source_sequence`。
- bit 7 为 0 时表示 continuation；它不是“本 item 不是第一项”，不能由
  `source_index > 0` 代替。
- 不持久化 converter 猜测的 `incoming_order_id` 或 `match_group_id`。下游可在
  per-product sequence 连续、无 gap 时，根据 first/continuation 与相同 `MATCH-TIME`
  派生 group；一旦 first packet 落在 gap 内，该 group 只能标记不完整。
- continuation `MATCH-TIME` 与 first packet 不一致、orphan continuation 或 item count
  越界都必须写 date/symbol/source sequence 日志，不能静默另起 group。

### cumulative volume / value

I024 的 `MATCH-TOTAL-QTY` 是 packet 结束后的 exchange cumulative quantity，而不是每个
item 自带的 cumulative。对 actual packet，可从末项向前严格展开：

```text
item_total_volume[i] = packet_match_total_qty
                       - sum(volume[j] for j > i in the same packet)
```

必须校验 packet item volume 总和不大于 `MATCH-TOTAL-QTY`，且与上一 actual summary 的
变化一致；gap 后以前者为 authoritative cumulative volume，并把缺失范围写入质量日志。

`total_value` 继续使用项目已锁定的统一口径：

```text
total_value += abs(price) * volume * multiplier
```

- quantity 是 contract；multiplier 与 currency 从同 generation basic-info join。
- calendar spread value 是价差绝对值的名义量，不是两条腿 gross notional、保证金或现金流。
- exchange 不提供累计 value。若 gap 遗漏 I024，I084 只能恢复 total volume 与部分统计，
  无法恢复遗漏价格路径；该 symbol 后续 `total_value` 必须由 manifest/log 标记为不完整，
  不能用 snapshot total volume 猜价格回填。
- trial item 不改变 actual `total_volume/total_value`；其 price/volume 只保留试撮事实。

### open / high / low / last

- `last_price/high/low` 按 Trade item wire 顺序只由 actual item 更新；I025/I084 S 用于
  recovery 和一致性校验。
- I025 的业务时间是 `SHOW-TIME`，不是 header `INFORMATION-TIME`。若将其输出为独立
  `HighLow` event，应同时保留二者；若只更新 builder state，也必须保存正确的 show time
  用于审计。
- 有开盘集合竞价时，open 是首个 actual I024 packet 的第一项；没有集合竞价而以逐笔
  撮合开盘时，open 是该盘逐笔撮合形成的最后撮合价位，位于首个 actual packet 的某一项；
  无集合竞价且无成交时，期货 official open 等于 reference price。
- 因此当前“第一项 actual trade 永远是 open”的实现不成立。正式迁移必须建立开盘模式/
  session state fixture，并用 I070/I071/I072 official open 做收盘校验；不能仅靠
  `open == 0` 判断未初始化，因为零价 spread 合法。
- intraday Orderbook 中 `total_volume=0` 时 actual OHLC/last 的零值表示尚无 actual trade；
  official no-trade open 属于 session statistics，不应通过未来收盘消息回填到盘前 row。

## packet-level `TradeStats`

I024 packet 尾部的三个 summary 是 exchange direct fact，但累计买/卖成交笔数不是
Orderbook level volume，也不是逐 item aggressor side。建议每个 I024 packet 另写一行：

```text
trading_day,market,symbol,exchange_ns,information_ns,local_ns,is_trial,
exchange_total_volume,total_buy_match_count,total_sell_match_count,
is_first_packet,source_sequence
```

- `exchange_total_volume` 对 actual packet 更新 actual cumulative state；trial packet 只按
  wire summary 保留，不覆盖 actual state。
- count 是累计成交笔数，不能分摊到 packet 内 individual Trade，也不能推导其 aggressor。
- I084 S 可作为 recovery/validation source，使用配套 I084 O 的
  `LAST-PROD-MSG-SEQ` 表示 as-of；它不能生成遗漏的 Trade rows。

如果实现阶段不需要单独查询这些 counts，可以先不发布 `TradeStats`，但必须从主
Orderbook 删除误导字段，并在 packet validation/log 中保留 summary。不能把 counts 改名为
bid/ask volume。

## `DerivedOrderbook<1>` 与 `OrderbookDelta`

### DerivedOrderbook

TAIFEX derived bid/ask 是交易所直接提供的一档，不是普通五档的第六档，也不是可从
outright book 无条件重算的研究因子。建议 actual-only record：

```text
trading_day,market,symbol,exchange_ns,local_ns,
derived_ask_price,derived_ask_volume,derived_bid_price,derived_bid_volume,
source_message,source_sequence
```

- I081 action 5 直接 overlay；price/volume 都为 0 表示清除。
- actual I083 先清空再完整替换；即使没有 derived entry，也发布 empty state 以表达清除。
- trial I083 按协议没有 derived order，不发布也不清空 actual derived state。
- 一条 source 同时修改 ordinary 与 derived 时，两张表可以共享
  `(symbol, exchange_ns, source_sequence)`。

前 2,000,000 frame 样本里，I081 ordinary action 为 514,851 项，derived overlay 为
672,494 项。derived 不是可忽略的极少数边界；独立输出也能避免 derived-only 更新产生大量
重复 ordinary rows。

### OrderbookDelta

主 Orderbook 保存 applied state，不应保留当前压缩后的 `orderbook_action=0/1`。若研究或
debug 需要 exact I081 delta，另写：

```text
trading_day,market,symbol,exchange_ns,local_ns,
book_type,side,action,level,price,volume,
source_sequence,source_index
```

- `book_type=ordinary/derived`；`side=bid/ask`；`action=new/change/delete/overlay`。
- `source_index` 保留同一 I081 内 entry 顺序；不能先按 side/action regroup 再应用。
- I083 是 snapshot，不伪造为一组 delta；需要 replay 时直接消费 snapshot + 后续 delta。

## 其他协议字段的分表边界

| 来源 | 文档字段 / 语义 | 建议归属 |
|---|---|---|
| I025 | day high/low 与 `SHOW-TIME` | builder validation/recovery；需要事件流时进入 `HighLow` |
| I030 | per-product 买卖累计委托笔数与 contract 数 | `OrderStats`，不是五档 total volume |
| I070 | session OHLC/close、委托/成交统计 | session/daily statistics |
| I071 | I070 + settlement price | session/daily statistics |
| I072 | I071 + open interest + block trade quantity | session/daily statistics；open interest 不进 Orderbook |
| I073 | calendar-spread close statistics | spread session/daily statistics |
| I084 O | full book + last product sequence | recovery state，不直接发布实时 market row |
| I084 S | first/last match、total volume、match counts、high/low、I030 totals | recovery/validation；按所属事实分表 |
| I084 P | product lifecycle 与 price-band status | `InstrumentStatus` |
| I010/I011 | reference、decimal locator、multiplier、currency、contract metadata | basic-info |
| I012 | 多阶涨跌停价 | price-limit dataset / validation，不重复进每行 book |
| I140 | system/product status | `InstrumentStatus` |

I030 的最小 `OrderStats` 可包含：

```text
trading_day,market,symbol,exchange_ns,local_ns,
buy_order_count,buy_order_volume,sell_order_count,sell_order_volume,
channel_id,channel_sequence
```

I030 没有 `PROD-MSG-SEQ`，因此这里保留 common header 的 channel identity；
`exchange_ns` 使用 header `INFORMATION-TIME`。`volume` 单位同样是 contract。
I072 的 `OPEN-INTEREST` 是交易日/session 统计值，
不是逐 I024 变化的盘中持仓序列；除非未来接入独立逐笔来源，不应把同一个收盘数复制到
全天 Orderbook 或 Trade。

## 可推导字段与禁止伪造字段

### 研究层按需计算，不写 source 主表

- mid、spread、microprice、level imbalance、book slope。
- 普通 N 档 bid/ask volume sum 与 volume-weighted book price。
- 连续无 gap 区间的 `delta_total_volume` / `delta_total_value`。
- 从 `Trade` 聚合的 bar、VWAP、trade count 和 price impact。
- 从 `OrderbookDelta` 统计的更新频率与 action count。
- gap-free 条件下由 first/continuation 推导的 incoming-order match group。

### 不得写成 exchange fact

- aggressor side：I024 不提供逐 item 主动买卖方向；累计 buy/sell match count 也不能分配
  到单笔成交。
- order id、incoming-order id、queue position、per-level order count。
- 遗漏 gap 内的逐笔价格、成交路径与精确 cumulative value。
- `information_ns - exchange_ns` 形式的 network latency。
- `volume / multiplier` 形式的 unit count；期货 volume 已是 contract，multiplier 是每口
  contract size。
- 从普通与 derived book 反推的 synthetic leg executions。

## 当前 Aries 实现必须修正的行为

后续 schema 实施除了改字段，还必须修正以下状态语义：

1. `ProcessTrade()` 当前直接跳过 I024 `MATCH-TIME` bytes，event 使用 header time；
   first/continuation bit 完全未保存，Trade item 也未输出。
2. I025 已解码 `SHOW-TIME` 但立即丢弃；high/low state 使用 header time。
3. trial I024 当前会覆盖 `last_price` 并增加 pending `trade_volume`；trial 只应输出
   `Trade(is_trial=1)`。
4. I081 不重置 `match_flag`，因此 row 可能继承上一条 I024/I083 的 trial flag；
   `is_trial` 必须来自触发当前 book 的 source。
5. I083 无条件 `ClearBook()`，trial snapshot 会覆盖 actual ordinary/derived state；必须使用
   独立 actual/trial book。
6. 当前 opening 永远取首个 actual Trade item；这不覆盖“无集合竞价、逐笔撮合开盘”的
   protocol 规则。
7. I084 S 的 `first_price` 可以覆盖已有较新 open；recovery merge 必须逐字段遵守 as-of
   sequence，不能回滚更晚 actual state。
8. `orderbook_action` 只在 New/Delete 设为 1，Change 与 derived Overlay 都变成 0；主表应
   删除，delta 表保存 exact action/type/index。
9. derived-only I081 当前仍写一条重复 ordinary row；拆表后分别按实际变更发布。
10. `total_value` 在 gap recovery 后无法恢复遗漏路径；当前虽记录 gap，却没有 generation
    manifest/schema-level invalidation，正式迁移必须补齐。
11. `localtime=exchtime` 是 offline compatibility fallback；新 manifest 必须说明具体来源。
12. 当前 `reference_price` 与 `symbol_id=-1` 每行重复；正式 schema 分别改为 basic-info join
    与删除。

## 与 Orion 的有意差异

Orion 的实际 TAIFEX `to_csv` 使用 Nova frontend options 的 `quill::CsvWriter` 和
40 列 `FactorCsvWriter`；它不是 `orderbook_format.h` 的完整 struct formatter。Orion 会把
I024 状态折叠到后续 I081/I083 row，不输出 Trade。Aries 后续设计有意区别如下：

- 保持单进程同步 converter，不依赖 SHM、runtime symbol pool 或 async reader。
- I024 使用 `MATCH-TIME`，并额外保存 `INFORMATION-TIME`；Orion builder 只使用 header
  `exchtime`，已转换的 `match_time` 没有进入 CSV。
- 每项 Trade 只累计自身 volume；Orion multi-item 聚合路径会把累计 packet volume 再次
  加进 total volume。
- value 使用 `abs(price) * volume * multiplier`；Orion 不乘 multiplier，且负价 spread
  会产生负 value。
- actual/trial Trade 与 book state 分开；Orion 与当前 Aries 都可能让 trial price 覆盖
  actual `last_price`。
- I025 采用 `SHOW-TIME` 并作为官方 high/low source；Orion 先覆盖 header time 后再比较
  show time，更新条件并不可靠。
- ordinary、derived、delta 和 packet statistics 分表；Orion to-csv 不输出 derived，
  当前 Aries 则把 derived 重复在 ordinary 宽表。
- 删除 runtime `symbol_id`、packed status、误命名 bid/ask totals、open interest 和
  per-row recovery flag。
- I083 actual/trial 都按独立 state 做完整 replace；I084 recovery 不伪装成实时 event。

## 真实数据证据

2026-07-07 完整日盘 dry-run（`13:46:00` cutoff 前）读取 61,605,862 条 message，当前
converter 模拟输出 54,086,067 条 orderbook row 与 4,839 条 basic-info row。当前日志还
记录 4 个 product sequence gap，均通过 snapshot recovery；这些 recovery 仍无法重建
遗漏的 exact Trade/value path。

同日之前已完成的前 2,000,000 frame 只读样本：

- 127,339 个 I024 packet 展开为 132,095 个 price/quantity item；3,066 个 packet 含多个
  item，样本单 packet 最大 21 项。
- 91,114 个 I024 packet 的 `MATCH-TIME` 与 header `INFORMATION-TIME` 不同。
- 1,853 个 I025 中，290 个 `SHOW-TIME` 与 header time 不同。
- I081 ordinary action 514,851 项，derived Overlay 672,494 项。
- I083 出现 39 个 empty book；empty snapshot 是正常协议状态，不是数据错误。
- 完整日盘有 1,757,829 条 I030，说明 per-product OrderStats 实际存在且量大。

这些结果只用于支持字段设计，不是新 schema 的 output baseline。正式实施后必须重新生成
并记录新文件的 row count、column count、hash、时间范围、gap summary 与 storage cost。

## 后续实施顺序

该设计经用户 review 后，应作为独立 L3 schema migration：

1. 锁定 TAIFEX 专属 enums、`Orderbook<5>`、`Trade`、market sentinel 与时间 fixtures。
2. 先建立 I024 multi-item/continuation、trial、negative/zero spread、item cumulative
   volume、MATCH/INFORMATION time 的失败测试。
3. 建立 I083 actual/trial/empty/market snapshot 和 I081 mixed ordinary/derived/action
   ordering fixture。
4. 分离 builder internal actual book、trial book、actual trade state 与 derived state。
5. 增加 Trade writer；再按需要增加 `TradeStats`、`DerivedOrderbook` 与
   `OrderbookDelta` writer。
6. 修正 I025 `SHOW-TIME`、open mode、I084 per-field as-of merge 与 gap/value invalidation。
7. 创建新 schema/version、文件命名与 generation manifest；不兼容双写、不覆盖旧文件。
8. 跑 focused CTest、完整 CTest、一天真实 full conversion 和独立全文件 scanner。
9. 测量 orderbook 去重、Trade/derived 分表后的 bytes、吞吐与压缩比，再确定全历史重建。

## 验收边界

- 每个 I024 price/quantity item 恰好一条 Trade，source-level key 唯一且 wire 顺序不变。
- Trade `exchange_ns=MATCH-TIME`，`information_ns=INFORMATION-TIME`；I025 使用
  `SHOW-TIME`。
- actual/trial ordinary book 独立；trial Trade/book 不改变 actual OHLC/volume/value。
- zero-price spread、empty level 与 trial market order 可无歧义区分。
- ordinary 与 derived updates 都不丢失；derived-only source 不制造 ordinary duplicate。
- I081 entries 严格按 `source_index` 应用；主 state 与 delta replay 结果一致。
- actual Trade row 的最终 item `total_volume` 等于 packet `MATCH-TOTAL-QTY`。
- I030、I024 counts、I072 open interest 均位于正确数据集，不再冒充盘口字段。
- gap/recovery/metadata/local-time fallback 由 date/symbol/sequence manifest 或日志审计；
  不可恢复的 Trade/value 不被静默伪造。

## 当前未实现边界

- 当前 `Orderbook<5>`、44/27 列 CSV、文件名和 `/tw_backup` 历史结果均未因本文改变。
- 当前没有 Trade、TradeStats、DerivedOrderbook、OrderbookDelta、OrderStats 或 HighLow CSV。
- 当前没有 schema version / generation manifest；quality 仍主要依赖逐日 converter log。
- 2026-07-07 真实样本证据只覆盖 futures 日盘；night session 与 options 需要独立设计和回归。
- opening mode、trial packet summary 和 orphan continuation 的完整真实 fixture 仍需在实施阶段
  从更多交易日取样验证，不能仅凭本文推断直接宣称已正确实现。
