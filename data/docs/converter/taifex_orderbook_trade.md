# TAIFEX Futures 逐笔行情字段设计

更新时间：2026-08-05

状态：**字段设计建议，尚未实现**。当前 converter 与已发布 CSV 仍以
`taifex.md` 记录的 44 列 orderbook、27 列 basic-info 为准。本文是下一版 TAIFEX
futures 逐笔 record、CSV schema 和状态边界的唯一设计事实源；实现前还需与用户逐项确认。

## 结论

TAIFEX 不应复制股票“成交组与 terminal 五档合并为一条 Orderbook”的设计。官方协议中：

- I024 是独立成交 packet，一项 price/quantity 对应一条 `Trade`；
- I081 是普通/衍生盘口增量，I083 是完整盘口 snapshot；
- I024/I025/I081/I083 共用 per-product `PROD-MSG-SEQ`，可按 sequence 确定性合并；
- I030 委托统计、I140 状态、I070-I073 session statistics 都是独立事实，不属于五档。

因此建议的持久化数据集为：

| 优先级 | record / dataset | 来源 | 建议 |
|---|---|---|---|
| P0 | `Orderbook<N>` | I081 / I083 | 普通 N 档 applied state；不重复成交状态 |
| P0 | `Trade` | I024 每个 price/quantity item | 一项一行，保留业务时间、发布时间和 packet 顺序 |
| P0 | `TradePacket` | I024 packet summary | 保存交易所累计量、买卖成交笔数和 match group 边界 |
| P1 | `DerivedOrderbook<1>` | I081 overlay / I083 snapshot | 衍生一档，与普通五档独立 |
| P1 | `HighLow` | I025 | 官方盘中 high/low checkpoint 与 `SHOW-TIME` |
| P1 | `OrderStats` | I030 | 累计买卖委托笔数与口数 |
| P1 | status datasets | I140 | 交易状态、涨跌停扩展、动态价格稳定措施事件 |
| P2 | `OrderbookDelta` | I081 entry | 可选 exact replay / microstructure 数据；体积很大 |
| P2 | `QuoteRequest` | I100 | 询价揭示事件 |
| P2 | underlying datasets | I060 / I064 | 标的现货与标的试算/状态 |
| P2 | session statistics | I070-I073 | 盘后统计；包含 open interest，不是盘中 tick |

运行时若策略需要“最近成交 + 当前盘口”的宽状态，可由同一 symbol 的有序事件更新内部
`MarketState`；不应为了运行时方便，把 Trade 字段重复写入每一条 Orderbook CSV。

## 范围与事实源

本设计只覆盖 futures 日盘，不包含夜盘交易日映射、options、block trade 逐笔或历史 schema
迁移。事实源按优先级为：

1. [TAIFEX《期貨逐筆行情資訊傳輸作業手冊 V1.5.1》](<../exchange/期貨逐筆行情資訊傳輸作業手冊(V1.5.1).pdf>)，尤其是 I024、I025、I030、
   I070-I073、I081、I083、I084、I100、I140 与 orderbook management examples。
2. Aries 的 `data/converter/taifex` protocol、decoder、builder、record 与 writer。
3. `/home/liuxiang/dev/orion` 的 TAIFEX data reader/data engine/orderbook builder、
   `FutureOrderbook<5>` 与 tools `to_csv` 路径。
4. 2026-08-03 日盘真实 dump 的独立全文件 scanner，以及 Aries/Orion 对 DIFH6 的实盘对照。

官方 wire field 是协议事实；本文提出的 normalized field 是 converter contract；mid、imbalance、
aggressor inference 等属于研究派生。三者不得混写。

## 全局 contract

### 类型、身份与 CSV

- 所有 price 与累计 value 使用 `double`；所有 quantity/volume/count 使用有符号
  `std::int64_t`，volume 单位为 contract。
- `market` 使用 enum underlying numeric value；建议 `3=TAIFEX`，CSV 直接输出数值。
- `symbol` 使用 `char[20]`，对应官方 `PROD-ID X(20)`。decoder 必须写满这 20 bytes；CSV
  writer bounded 读取并去除尾端空白/零，不依赖第 20 byte 后的 `\0`。2026-08-03 实盘最长
  symbol 为 11 bytes，但 schema 不能据此缩短官方上限。
- `symbol_id` 是单次运行内 dense id，保留在高频 C++ record 以优化 catalog/state 直接索引，
  但不写 CSV，也不承诺跨日稳定。
- `trading_day` 由文件分区、generation manifest 与同 generation basic-info 表达，不在每条
  高频 record/CSV 重复。夜盘实现前必须先锁定 trading-day mapping，不能仅从日历日期推断。
- enum、source message、side、action 与 raw state CSV 均输出 underlying integer，不输出字符串。
- 新 struct 无构造函数、默认成员初始化或 virtual function；实现时必须以 `static_assert`
  锁定 trivial、standard-layout、trivially-copyable，不使用 `#pragma pack`。

### 时间

所有 normalized timestamp 都是 Unix epoch ns。官方时钟为台湾本地时间，converter 结合有效
交易日期转换；当前只允许日盘。

| 字段 | 语义 |
|---|---|
| `exchange_ns` | 当前业务事件的官方业务时间；Trade 用 I024 `MATCH-TIME`，HighLow 用 I025 `SHOW-TIME`，Orderbook 用 I081/I083 header `INFORMATION-TIME` |
| `information_ns` | common header `INFORMATION-TIME`；只有业务时间与发布时间可能不同的 record 才单独保存 |
| `local_ns` | 本机 receive/capture time；offline dump 没有该字段时使用 `information_ns` fallback，并在 manifest 写明来源 |

`information_ns - exchange_ns` 只能表示交易所业务时间到发布 header 时间之差，不能冒充
network latency。offline 的 `local_ns` 也不得用于 latency 研究。

### 排序与唯一键

- `PROD-MSG-SEQ` 按 product 递增，并由 I024/I025/I081/I083 共用；它是 symbol event
  continuity key，不是 Trade-only 或 Orderbook-only row number。
- I024 一包多项用 `source_index` 保序；I081 一包多 entry 也用 `source_index` 保序。
- common header `CHANNEL-ID/CHANNEL-SEQ` 用于 multicast transport gap 检查；除 I030/I100
  这类没有 `PROD-MSG-SEQ` 的记录外，不在每条高频 CSV 重复。
- 文件内确定性 merge key 为 `(symbol, source_sequence, source_index)`；不同 record 类型按
  product sequence 合并，不按 CSV 文件写出先后猜测。

### actual、trial、snapshot 与 recovery

- actual ordinary book 与 trial ordinary book 必须是两份独立 state。
- actual I024 只更新 actual trade state；trial I024 只发布 trial Trade，不污染 actual
  last/OHLC/volume/value。
- I083 必须先清空对应 lane 再完整替换；trial I083 不能清空 actual ordinary/derived state。
- I084 是 recovery snapshot。I084 O/S/P 用于恢复 state 和质量审计，不能伪装成发生在恢复
  时刻的 I083/I024/I140 实时事件。
- gap 可恢复 book 与部分累计统计，但不能恢复遗漏 I024 的逐项价格路径。缺失成交额只可按
  已锁定的 missing-volume 规则估算并写 quality summary，不能称为 exchange value。

## 当前 44 列 Orderbook 的迁移建议

当前 C++ `Orderbook<N>` 与 CSV 把 book、trade state、derived、source action 和 metadata 混在
一行。逐字段建议如下：

| 序号 | 当前字段 | 新 record | CSV | 建议 |
|---:|---|---|:---:|---|
| C01 | `trading_day` | partition / manifest | 否 | 从高频行删除；由路径、manifest、basic-info 表达 |
| C02 | `market`（当前仅 CSV 常量） | 所有高频 record | 是 | 放入 record，输出 enum numeric value |
| C03 | `symbol` | 所有 product record | 是 | 改为 `char[20]`，保留 official product id |
| C04 | `symbol_id`（当前 CSV 恒 `-1`） | internal record | 否 | 在 C++ 保留 dense id，CSV 删除 |
| C05 | `exchtime` | 各 record | 是 | 改名 `exchange_ns`，按 record 使用正确业务时间 |
| C06 | `localtime` | 各 record | 是 | 改名 `local_ns`，offline fallback 写入 manifest |
| C07 | `reference_price` | basic-info / price-limit | 否 | 从 Orderbook 删除，按 generation join |
| C08 | `open` | derived/session statistics | 否 | 从 Orderbook 删除；盘中由 actual Trade/session mode 推导，盘后用 I070-I073 校验 |
| C09 | `high` | `HighLow` / derived | 否 | 从 Orderbook 删除；I025 是官方 checkpoint |
| C10 | `low` | `HighLow` / derived | 否 | 同 C09 |
| C11 | `last_price` | `Trade.price` / runtime state | 否 | 从 Orderbook 删除，避免把旧成交重复写到每次 book update |
| C12 | `trade_volume` | `Trade.volume` / `TradePacket.packet_volume` | 否 | 从 Orderbook 删除；当前“到下一条 book 为止”的窗口依赖发布时机 |
| C13 | `total_volume` | `TradePacket.exchange_total_volume` | 否 | 移到 packet summary，不在 book 重复 |
| C14 | `total_value` | `TradePacket.total_value` | 否 | 移到 packet summary；这是 converter 计算值，不是 exchange book field |
| C15 | `total_buy_count` | `TradePacket.total_buy_match_count` | 否 | 移出；不是 bid volume，也不能代表 aggressor |
| C16 | `total_sell_count` | `TradePacket.total_sell_match_count` | 否 | 同 C15 |
| C17 | `ask_price[N]` | `Orderbook<N>` | 是 | 保留普通卖方 N 档，signed `double` |
| C18 | `ask_volume[N]` | `Orderbook<N>` | 是 | 保留，contract 数 |
| C19 | `bid_price[N]` | `Orderbook<N>` | 是 | 保留普通买方 N 档，signed `double` |
| C20 | `bid_volume[N]` | `Orderbook<N>` | 是 | 保留，contract 数 |
| C21 | `derived_ask_price` | `DerivedOrderbook<1>` | 否 | 从普通 Orderbook 移出 |
| C22 | `derived_ask_volume` | `DerivedOrderbook<1>` | 否 | 同 C21 |
| C23 | `derived_bid_price` | `DerivedOrderbook<1>` | 否 | 同 C21 |
| C24 | `derived_bid_volume` | `DerivedOrderbook<1>` | 否 | 同 C21 |
| C25 | `match_flag` | `is_trial` | 是 | 只描述当前 source lane；不得继承前一条 I024/I083 |
| C26 | `build_type` | `source_message` | 是 | 删除不透明 `0/3`，输出 typed I081/I083 numeric code |
| C27 | `orderbook_action` | `OrderbookDelta.action` | 否 | 从主表删除；当前 0/1 压缩丢失 Change/Delete/Overlay |
| C28 | `sequence` | `source_sequence` | 是 | 明确为 common per-product `PROD-MSG-SEQ` |

## `Orderbook<N>`

### 建议 C++ schema

```cpp
enum class Market : std::uint8_t {
  kTaifex = 3,
};

enum class BookSource : std::uint8_t {
  kI081 = 81,
  kI083 = 83,
};

template <std::size_t N>
struct Orderbook {
  char symbol[20];

  std::int64_t exchange_ns;
  std::int64_t local_ns;

  std::int32_t symbol_id;

  Market market;
  BookSource source_message;
  std::uint8_t is_trial;
  std::uint8_t best_ask_is_market;
  std::uint8_t best_bid_is_market;

  double ask_price[N];
  std::int64_t ask_volume[N];
  double bid_price[N];
  std::int64_t bid_volume[N];

  std::uint64_t source_sequence;
};
```

字段逻辑顺序不等于最终内存排布；实现时可在不改变 schema 的前提下按自然对齐重排，并用
`sizeof/offsetof` 测试锁定当前 ABI。

### 字段表

| 序号 | 字段 | CSV | 来源 / 单位 | 建议与语义 |
|---:|---|:---:|---|---|
| OB01 | `symbol` | 是 | I081/I083 `PROD-ID` | 保留；20-byte bounded product id |
| OB02 | `market` | 是 | normalized enum | 保留，输出 `3` |
| OB03 | `exchange_ns` | 是 | header `INFORMATION-TIME` | 当前 book state 完整可用的 exchange publish time |
| OB04 | `local_ns` | 是 | capture / offline fallback | 保留；fallback 不可研究 latency |
| OB05 | `symbol_id` | 否 | converter catalog | 内部保留，CSV 删除 |
| OB06 | `source_message` | 是 | I081 / I083 | 区分 incremental-applied state 与 full snapshot state |
| OB07 | `is_trial` | 是 | I083 `CALCULATED-FLAG` | `0=actual,1=trial`；I081 恒为 0 |
| OB08 | `ask_price[N]` | 是 | ordinary sell levels | signed price；零价 spread 合法 |
| OB09 | `ask_volume[N]` | 是 | contracts | 空档 `price=0,volume=0` |
| OB10 | `bid_price[N]` | 是 | ordinary buy levels | signed price；零价 spread 合法 |
| OB11 | `bid_volume[N]` | 是 | contracts | 空档 `price=0,volume=0` |
| OB12 | `best_ask_is_market` | 是 | I083 trial sentinel | trial best ask 为市价时 1，并把 normalized price 写 0 |
| OB13 | `best_bid_is_market` | 是 | I083 trial sentinel | trial best bid 为市价时 1，并把 normalized price 写 0 |
| OB14 | `source_sequence` | 是 | `PROD-MSG-SEQ` | 触发本 state row 的 product sequence |

### 发布与盘口应用规则

- I081 entry 必须按 wire `source_index` 顺序逐一应用；New/Delete 会移动后续档位，不能先
  regroup 再更新。
- 一条 I081 含至少一个 ordinary action 时，应用整包后最多发布一条 `Orderbook`。
  derived-only I081 不发布重复 ordinary row。
- actual I083 清空并替换 actual ordinary/derived state；count=0 也发布 empty ordinary row。
- trial I083 只清空并替换 trial ordinary state。协议规定 trial 没有 derived order，不能
  因 trial snapshot 清空 actual derived state。
- trial best market bid sentinel 为 `+999999999`，best market ask sentinel 为
  `-999999999`。normalized price 写 0 并用 market flag 区分；不得用 `price==0` 推断 empty
  或 market，因为 calendar spread 的 0 是合法 limit price。
- I084 O 只恢复 state 与 as-of sequence，不生成伪实时 Orderbook row。

## `Trade`

### 建议 C++ schema

```cpp
struct Trade {
  char symbol[20];

  std::int64_t exchange_ns;
  std::int64_t information_ns;
  std::int64_t local_ns;

  std::int64_t volume;
  double price;

  std::uint64_t source_sequence;
  std::uint64_t match_group_sequence;

  std::int32_t symbol_id;

  Market market;
  std::uint8_t is_trial;
  std::uint8_t is_first_packet;
  std::uint8_t source_index;
};
```

### 字段表

| 序号 | 字段 | CSV | 来源 / 单位 | 建议与语义 |
|---:|---|:---:|---|---|
| T01 | `symbol` | 是 | I024 `PROD-ID` | 保留 official product id |
| T02 | `market` | 是 | normalized enum | 输出 `3` |
| T03 | `exchange_ns` | 是 | I024 `MATCH-TIME` | 成交/试撮业务发生时间 |
| T04 | `information_ns` | 是 | header `INFORMATION-TIME` | packet exchange publish time；不能用来替代 MATCH-TIME |
| T05 | `local_ns` | 是 | capture / fallback | offline fallback 为 information time |
| T06 | `symbol_id` | 否 | internal catalog | C++ 保留，CSV 删除 |
| T07 | `is_trial` | 是 | `CALCULATED-FLAG` | actual/trial 必须独立消费 |
| T08 | `price` | 是 | 当前 item signed price | spread 可负或为零；全部使用 `double` |
| T09 | `volume` | 是 | 当前 item quantity | contract 数；不同 item 不合并 |
| T10 | `is_first_packet` | 是 | bitmap bit 7 | 1 表示本 packet 开始一个 incoming-order match group |
| T11 | `source_sequence` | 是 | packet `PROD-MSG-SEQ` | 同 packet 的全部 item 相同 |
| T12 | `source_index` | 是 | converter 解析顺序 | FIRST item 为 0，extra item 为 1..70 |
| T13 | `match_group_sequence` | 是 | converter monotonic id | 同一 first+continuation group 共用；不是 exchange order id |

一项 price/quantity 对应一行 Trade。source-level 唯一键是
`(symbol, source_sequence, source_index)`；`match_group_sequence` 只为直接识别官方
first/continuation group：在每个 partition/symbol 的完整 I024 流中，每遇到 first packet
递增一次，continuation 继承当前值。遇到 gap、orphan continuation 或 continuation 的
`MATCH-TIME` 与 first 不一致时，写 date/symbol/sequence quality log；不新增伪造 order id。
Trade dataset 的 source 恒为 I024，由 schema/manifest 锁定，不在每行重复 `source_message`。

I024 bitmap 低 7 bits 是“FIRST 后额外 item 数”，最大 70，所以 packet 最多 71 条 Trade。
官方示例明确一张 incoming order 的 100 笔成交会拆为 first packet 与 continuation packet，
且 continuation 使用相同 `MATCH-TIME`。2026-08-03 没出现 continuation 不能成为不实现它的
理由。

### 不加入 `trade_side`

I024 不提供 aggressor side，也不提供 resting/incoming order id。I024 尾部累计买进/卖出
成交笔数表示成交双方统计，不是主动方向：2026-08-03 的 477,489 个 actual packet 中，
两个累计 count 每包都同时增长，`buy-only=0`、`sell-only=0`。因此：

- `Trade` 不保存 `trade_side`；
- 不从累计 count、下一条五档或单档 volume change 伪造 exchange fact；
- 研究层若做 Lee-Ready、quote test 或 book-delta inference，应输出独立 derived dataset，
  带 method、as-of quote、confidence/unknown，不覆盖 source Trade。

## `TradePacket`

packet summary 是 exchange direct fact，不能重复分摊到每个 Trade item。建议每个 I024
packet 一行：

| 序号 | 字段 | CSV | 来源 / 单位 | 建议与语义 |
|---:|---|:---:|---|---|
| TP01 | `symbol` | 是 | I024 | 同 Trade |
| TP02 | `market` | 是 | normalized enum | 输出 `3` |
| TP03 | `exchange_ns` | 是 | `MATCH-TIME` | group 业务时间 |
| TP04 | `information_ns` | 是 | header time | packet 发布时间 |
| TP05 | `local_ns` | 是 | capture / fallback | 同 Trade |
| TP06 | `symbol_id` | 否 | internal catalog | C++ 保留，CSV 删除 |
| TP07 | `is_trial` | 是 | `CALCULATED-FLAG` | trial summary 不更新 actual state |
| TP08 | `is_first_packet` | 是 | bitmap bit 7 | group start marker |
| TP09 | `item_count` | 是 | `1 + bitmap[6:0]` | packet 内 Trade item 数，1..71 |
| TP10 | `packet_volume` | 是 | converter sum(items) | 当前 packet 明示成交口数 |
| TP11 | `exchange_total_volume` | 是 | `MATCH-TOTAL-QTY` | packet 结束后的 exchange cumulative contracts |
| TP12 | `total_buy_match_count` | 是 | `MATCH-BUY-CNT` | exchange cumulative buy-side match count；不是主动买 |
| TP13 | `total_sell_match_count` | 是 | `MATCH-SELL-CNT` | exchange cumulative sell-side match count；不是主动卖 |
| TP14 | `total_value` | 是 | converter cumulative currency | actual packet 为累计名义成交额；trial packet 固定输出 0，且不更新 actual value |
| TP15 | `source_sequence` | 是 | `PROD-MSG-SEQ` | packet source key |
| TP16 | `match_group_sequence` | 是 | converter id | 与 Trade T13 相同 |

`total_value` 的统一口径为：

```text
observed_value = sum(abs(item.price) * item.volume * multiplier)
volume_diff = exchange_total_volume - previous_exchange_total_volume
missing_volume = volume_diff - packet_volume
total_value += observed_value
             + abs(last_actual_price) * missing_volume * multiplier
```

- 无 gap 时 `missing_volume=0`，全部使用逐项真实成交价。
- gap/recovery 后只对 exchange cumulative volume 证明缺失的部分使用当前
  `last_actual_price` 补值；记录 symbol、source sequence、volume diff、observed/missing
  volume 和补值价格。不得把补值称为精确 exchange value。
- multiplier/currency 必须来自同 generation basic-info；缺少 multiplier 时记录问题，不能
  静默使用 1。
- calendar spread 使用 `abs(price)`，因此 value 非负；它表示价差合约的项目统一名义口径，
  不是两腿 gross notional、保证金或现金流。
- 若 exchange summary 与当前有序状态出现 `volume_diff < packet_volume`，这是 sequence/state
  contract 破坏，写错误日志并隔离该 symbol 的累计 state；不是靠 clamp 掩盖。

`open/high/low/last` 不在 TradePacket 重复。last 就是最后一条 actual Trade price；盘中 OHLC
由 actual Trade 按 session 规则派生，并由 I025 与 I070-I073 校验。官方开盘规则还包括：

- 有集合竞价：首个 actual I024 packet 的第一项为 open；
- 无集合竞价、连续撮合开盘：open 是开盘撮合批次的最后成交价，不保证是 packet 第一项；
- 无成交：futures official open/reference/close 相同。

因此不能只用“第一笔 actual trade”或 `open==0` 判断开盘，零价 spread 本身合法。

## `DerivedOrderbook<1>`

TAIFEX derived bid/ask 是交易所直接提供的一档，不是普通五档的第六档，也不能无条件从
outright book 重算。建议 actual-only record：

| 序号 | 字段 | CSV | 来源 / 单位 | 建议与语义 |
|---:|---|:---:|---|---|
| DB01 | `symbol` | 是 | I081/I083 | official product id |
| DB02 | `market` | 是 | enum | 输出 `3` |
| DB03 | `exchange_ns` | 是 | header time | derived state publish time |
| DB04 | `local_ns` | 是 | capture / fallback | 同 Orderbook |
| DB05 | `symbol_id` | 否 | internal | C++ 保留，CSV 删除 |
| DB06 | `ask_price` | 是 | derived sell | signed double；empty 为 0/0 |
| DB07 | `ask_volume` | 是 | contracts | derived sell quantity |
| DB08 | `bid_price` | 是 | derived buy | signed double；empty 为 0/0 |
| DB09 | `bid_volume` | 是 | contracts | derived buy quantity |
| DB10 | `source_message` | 是 | I081/I083 | numeric source |
| DB11 | `source_sequence` | 是 | product sequence | source key |

I081 action 5 是 overlay；price/volume 都为 0 表示清除。一条 source 同时更新 ordinary 与
derived 时，两张 state 表共享 `(symbol, exchange_ns, source_sequence)`。actual I083 先清空
再替换，缺少 derived entry 也要发布 empty state；trial I083 不发布 derived，也不清空
actual derived。

## 可选 `OrderbookDelta`

I081 entry 是 level action，不是逐笔委托：协议没有 order id、queue position 或每档订单数。
若 exact replay、撤挂统计或 builder debug 需要，建议一 entry 一行：

| 序号 | 字段 | CSV | 来源 / 单位 | 建议与语义 |
|---:|---|:---:|---|---|
| OD01 | `symbol` | 是 | I081 | product id |
| OD02 | `market` | 是 | enum | 输出 `3` |
| OD03 | `exchange_ns` | 是 | header time | source publish time |
| OD04 | `local_ns` | 是 | capture / fallback | 同 Orderbook |
| OD05 | `symbol_id` | 否 | internal | C++ 保留，CSV 删除 |
| OD06 | `book_type` | 是 | entry type | `0=ordinary,1=derived` normalized numeric |
| OD07 | `side` | 是 | wire type | `0=bid,1=ask` numeric |
| OD08 | `action` | 是 | wire action | `0=New,1=Change,2=Delete,5=Overlay` |
| OD09 | `level` | 是 | wire level | 1-based；derived overlay 按协议处理 |
| OD10 | `price` | 是 | signed double | action payload price |
| OD11 | `volume` | 是 | contracts | action payload quantity |
| OD12 | `source_sequence` | 是 | product sequence | message source key |
| OD13 | `source_index` | 是 | entry index | 同一 I081 内严格 wire order |

I083 是 snapshot，不伪造为一组 delta。2026-08-03 日盘 I081 有 51,183,942 个 entry，
delta 表的存储量可能高于 applied state；只有明确需要 exact action path 时才发布。

## I025 `HighLow`

I025 的 `SHOW-TIME` 是最近一次价格穿越当前 high/low 的业务时间；header
`INFORMATION-TIME` 是消息发布时间。两者都要保存：

| 序号 | 字段 | CSV | 来源 / 单位 | 建议与语义 |
|---:|---|:---:|---|---|
| HL01 | `symbol` | 是 | I025 | product id |
| HL02 | `market` | 是 | enum | 输出 `3` |
| HL03 | `exchange_ns` | 是 | `SHOW-TIME` | high/low 生效业务时间 |
| HL04 | `information_ns` | 是 | header time | source publish time |
| HL05 | `local_ns` | 是 | capture / fallback | offline fallback 为 information time |
| HL06 | `symbol_id` | 否 | internal | C++ 保留，CSV 删除 |
| HL07 | `high` | 是 | signed price | official intraday high checkpoint |
| HL08 | `low` | 是 | signed price | official intraday low checkpoint |
| HL09 | `source_sequence` | 是 | product sequence | source key |

它既可发布为稀疏研究数据，也必须用于验证/recovery actual Trade 派生 high/low。2026-08-03
有 9,870 条 I025，其中 864 条 `SHOW-TIME != INFORMATION-TIME`。

## I030 `OrderStats`

I030 是 per-product 累计委托统计，不是可见五档总量，也不能还原 order id：

| 序号 | 字段 | CSV | 来源 / 单位 | 建议与语义 |
|---:|---|:---:|---|---|
| OS01 | `symbol` | 是 | I030 | product id |
| OS02 | `market` | 是 | enum | 输出 `3` |
| OS03 | `exchange_ns` | 是 | header time | official publish time |
| OS04 | `local_ns` | 是 | capture / fallback | 同上 |
| OS05 | `symbol_id` | 否 | internal mapping | 能映射时保留，CSV 删除 |
| OS06 | `buy_order_count` | 是 | cumulative count | 买方累计委托笔数 |
| OS07 | `buy_order_volume` | 是 | cumulative contracts | 买方累计委托口数 |
| OS08 | `sell_order_count` | 是 | cumulative count | 卖方累计委托笔数 |
| OS09 | `sell_order_volume` | 是 | cumulative contracts | 卖方累计委托口数 |
| OS10 | `channel_id` | 是 | common header | I030 没有 product sequence，需保留 transport identity |
| OS11 | `channel_sequence` | 是 | common header | I030 source ordering / gap audit |

2026-08-03 日盘有 1,360,652 条 I030，足以形成独立 order-flow 研究表；不能把它的 volume
复制进每条五档。

## I100 `QuoteRequest`

I100 是询价揭示事件，建议独立稀疏表：

| 序号 | 字段 | CSV | 来源 / 单位 | 建议与语义 |
|---:|---|:---:|---|---|
| QR01 | `symbol` | 是 | `PROD-ID-S X(10)` | 按 basic-info/catalog 映射 |
| QR02 | `market` | 是 | enum | 输出 `3` |
| QR03 | `exchange_ns` | 是 | body `DISCLOSURE-TIME` | 询价揭示时间 |
| QR04 | `information_ns` | 是 | header time | packet publish time |
| QR05 | `local_ns` | 是 | capture / fallback | offline fallback 为 information time |
| QR06 | `duration_seconds` | 是 | body | 揭示持续秒数 |
| QR07 | `channel_id` | 是 | header | transport source |
| QR08 | `channel_sequence` | 是 | header | source ordering |

2026-08-03 日盘有 210 条 I100。它不是 Trade，也不改变 Orderbook levels。

## underlying 数据

### I060 `UnderlyingSpot`

| 字段 | 来源 / 建议 |
|---|---|
| `kind_id` | I060 `KIND-ID`；按期货/选择权契约类别关联 |
| `market` | `3=TAIFEX` |
| `exchange_ns` | body time；缺少时以 header time 为事件时间并保留 `information_ns` |
| `information_ns` / `local_ns` | publish / capture time |
| `field_bitmap` | 原始 optional-field bitmap，CSV 输出 numeric value |
| `last_price` | bitmap 揭示时有效，否则 CSV 为空 |
| `bid_price` / `ask_price` | bitmap 揭示时有效，否则为空 |
| `fixing_price` | bitmap 揭示时有效，否则为空 |
| `source_date` | body 提供日期时保存，不能与 futures trading day 混用 |
| `channel_id` / `channel_sequence` | I060 没有 product sequence，保留 header source |

### I064 `UnderlyingTrialStatus`

| 字段 | 来源 / 建议 |
|---|---|
| `kind_id` | I064 contract kind |
| `market` | `3=TAIFEX` |
| `exchange_ns` / `information_ns` / `local_ns` | body event time、header publish time、capture time |
| `value` | 官方 underlying trial value，signed `double` |
| `status` | 原始 bitmap numeric value，至少保留 delayed-open/delayed-close bits |
| `channel_id` / `channel_sequence` | transport ordering |

2026-08-03 日盘有 348,124 条 I060 与 12,990 条 I064。它们适合基差、标的状态研究，
但不应塞入每个期货产品的 Orderbook。

## I140 状态事件

I140 的 scope 可以是 flow group、contract kind 或 product，不适合做一个塞满 nullable 字段的
宽 `InstrumentStatus`。建议按语义拆三张稀疏表，并共同保留
`market, information_ns, local_ns, function_code, scope_type, scope_id,
channel_id, channel_sequence`。

### `TradingStatusEvent`

| 字段 | 来源 / 语义 |
|---|---|
| common identity/time/source | 上述共同字段 |
| `reason` | I140 200/201 原因代码；其他 function 无原因时为空 |
| `break_ns` | 200 暂停时间 |
| `start_ns` | 201 恢复接受委托时间 |
| `reopen_ns` | 201 重新开盘时间 |
| `status` | normalized numeric：302 开始接受、305 不可取消、304 开盘、306 收盘不再恢复；同时保留原始 `function_code` |

### `PriceLimitStatusEvent`

| 字段 | 来源 / 语义 |
|---|---|
| common identity/time/source | I140 100/101 |
| `level` | 涨跌停扩展层级 |
| `expand_type` | 扩展类型 numeric value |
| `status` | `100=即将扩展`、`101=已经扩展`，直接保留 function code |

### `DynamicBandingEvent`

| 字段 | 来源 / 语义 |
|---|---|
| common identity/time/source | I140 400-405 |
| `reason` | 暂停/恢复/调整原因 |
| `effective_ns` | body 提供的生效时间；另保留 information time |
| `range` | 动态价格稳定范围，按 official decimal locator 转 `double` |
| `side` | body 指定买/卖侧时输出 numeric value |
| `status` | 400/403 suspend/announce、401/404 resume/announce、402/405 adjust/announce；保留 function code |

2026-08-03 日盘 I140 共 199 条：302/304/305 各 5 条、306 3 条，400 1 条、401
49 条、402 131 条；另有一条 306 在当前日盘 cutoff 后。I084 P 是 recovery snapshot 的
产品状态、动态 banding 状态、扩展层级与 long/short range，不应伪造成 I140 event；可更新
runtime state，并进入 recovery audit。

## I070-I073 session statistics

这些消息是 session/daily 统计，不是逐笔。2026-08-03 它们全部出现在当前
`13:46:00` cutoff 后，需由独立盘后阶段消费，不能为了得到 open interest 放宽实时日盘边界。

建议统一稀疏 `SessionStatistics` schema；不适用字段写 CSV empty，而不是写 0，因为 calendar
spread 的 0 与负价都合法：

| 序号 | 字段 | 来源 | 语义 |
|---:|---|---|---|
| SS01 | `symbol` | I070-I073 | product id |
| SS02 | `market` | normalized | 输出 `3` |
| SS03 | `information_ns` | header | official publish time |
| SS04 | `local_ns` | capture/fallback | offline fallback |
| SS05 | `source_message` | I070-I073 | numeric code，决定有效字段集合 |
| SS06 | `term_high` / `term_low` | I070-I073 | 契约期间高低价 |
| SS07 | `session_high` / `session_low` | I070-I073 | 本 session 高低价 |
| SS08 | `open` | I070-I073 | official session open |
| SS09 | `last_bid` / `last_ask` | I070-I073 | session end bid/ask |
| SS10 | `close` | I070-I073 | official close |
| SS11 | `buy_order_count` / `buy_order_volume` | I070-I073 | session 累计买委托统计 |
| SS12 | `sell_order_count` / `sell_order_volume` | I070-I073 | session 累计卖委托统计 |
| SS13 | `trade_count` / `trade_volume` | I070-I073 | session 成交笔数/口数 |
| SS14 | `combined_buy_order_count` / `combined_buy_order_volume` | I070-I072 | 含协议定义组合范围的买委托统计 |
| SS15 | `combined_sell_order_count` / `combined_sell_order_volume` | I070-I072 | 含协议定义组合范围的卖委托统计 |
| SS16 | `combined_trade_volume` | I070-I072 | official `COMBINE-TOTAL-QTY`；协议没有对应 combined trade count |
| SS17 | `settlement_price` | I071/I072 | official settlement |
| SS18 | `open_interest` | I072 | session/daily open interest；不进入 Orderbook/Trade |
| SS19 | `block_trade_volume` | I072 | block trade contracts |
| SS20 | `channel_id` / `channel_sequence` | header | source ordering/audit |

I072 的 price statistics 是 regular session；其他十二项统计按官方定义可能合并夜盘/日盘与
一般/鉅額范围。夜盘实现时必须再锁定 session scope，不能把它们无标记并入日盘累计。
I073 是 calendar-spread statistics，official NULL sentinel 为 `-999999999`，因为 spread
zero price 有效；decoder 必须先按 source null 规则转为空值。

2026-08-03 cutoff 后分别有 I070 13,384、I071 1,673、I072 5,022、I073 8,541 条。

## I084 recovery snapshot

I084 是明确带 A/O/S/P/Z 区段的 recovery stream，不是第二套实时逐笔行情。其字段必须被
decoder 完整理解，但默认只更新 builder state 与 quality audit：

| 区段 | official fields | 可恢复事实 | 持久化建议 |
|---|---|---|---|
| A | recovery begin | snapshot batch 开始 | generation log/manifest 记录 batch id、channel 与时间 |
| O | product id、`LAST-PROD-MSG-SEQ`、ordinary/derived full levels | as-of sequence 的完整盘口 | 恢复 actual ordinary/derived state；不发布伪 I083 row |
| S | last/first match、total volume、buy/sell match counts、high/low、I030 buy/sell order count/volume | as-of sequence 的成交与委托累计 checkpoint | 恢复/校验 TradePacket、HighLow、OrderStats state；不能生成遗漏 Trade items 或精确 value |
| P | expand-up/down level、7-byte product status、dynamic-banding status、long/short expansion range | 产品与价格稳定状态 snapshot | 更新 status state；不发布伪 I140 event |
| Z | recovery end | snapshot batch 完整结束 | 只有 A..Z 完整且 sequence 条件满足时标记 recovery success |

建议质量输出按 recovery batch 和 symbol 记录：`requested/expected sequence`、
`snapshot_last_sequence`、恢复的 state components、是否闭合 A..Z、是否成功衔接下一条 realtime
product sequence。它属于 summary/log，不复制到每条高频 record。I084 无法恢复遗漏 I024 的
每项 price/quantity，尤其不能把 S 的 total volume 反推成虚构 Trade。

## static/reference、公告与 transport 的边界

| 来源 | 归属 | 逐笔 CSV 建议 |
|---|---|---|
| I010 | futures basic-info：reference、decimal locator、multiplier、currency、dates、flow group 等 | 不进入高频行；现有 basic-info 继续扩展 |
| I011 | spread basic-info | 同上 |
| I012 | 多阶涨跌停价 | 独立 price-limit/basic generation；不在每条 book 重复 |
| I120 | stock futures/options 与 stock id/underlying mapping | basic/reference dataset |
| I130 | contract adjustment | reference event/dataset，不作为 market tick |
| I050 | 公告文字 | 需要时独立 announcement dataset；不为数字研究主线解析 Big5 text |
| common header version/body length | decoder/manifest | 不在每行重复 |
| checksum/trailer | input validation/quality log | 错误按 date/channel/offset 记录，继续处理可定位的下一 frame |
| I001/I002 等控制消息 | session/sequence quality log | 不伪造 product market row |
| I084 A/O/S/P/Z | recovery state/audit | 不发布伪实时 Trade/Orderbook/status row |

`taifex.md` 继续作为当前 basic-info 与现有 converter 行为的事实源；下一版实现时应另开
basic-info schema review，不在本文复制 I010/I011/I012/I120/I130 全部静态字段。

## 可推导与禁止伪造

### 研究层按需推导

- mid、spread、microprice、level imbalance、book slope；
- N 档 bid/ask volume sum、VWAP book price；
- actual Trade 的 OHLC、bar、VWAP、trade count、price impact；
- gap-free `TradePacket` 的 cumulative volume/value delta；
- `OrderbookDelta` action frequency，以及五档内可观察的挂单/撤单变化；
- 基于 `match_group_sequence` 的一张 incoming order 撮合组统计；
- 使用明确算法、as-of quote 与 unknown 状态的 aggressor inference。

### 不得写成 exchange fact

- aggressor side、incoming/resting order id、queue position、per-level order count；
- 五档之外的委托/撤单路径，或把 level Change/Delete 等同于单一订单行为；
- gap 内遗漏的逐项成交价格和精确成交路径；
- `information_ns - exchange_ns` 形式的 network latency；
- `volume / multiplier` 形式的 unit count：期货 volume 已是 contract，multiplier 是每口大小；
- 从 ordinary/derived book 反推的 synthetic leg executions。

## 2026-08-03 真实数据证据

输入：`/tw_backup/data/tw/raw/future/taifex_20260803.dump`

```text
size = 5,423,972,003 bytes
sha256 = b345f94c70288c3857388b64efbdf0f75bd66f31966cdce13ebb2a951a73d3c1
frames = 53,482,242
day frames before 13:46:00 = 52,519,771
```

独立全文件 scanner 得到：

- I024：563,520 packet，591,366 item；477,489 actual、86,031 trial；20,728 个
  multi-item packet，单包最大 37 项；当日所有 packet 都是 first，未出现 continuation。
- 112,461 个 I024 packet 的 `MATCH-TIME != INFORMATION-TIME`。
- 477,489 个 actual packet 的累计买/卖成交 count 都同时增长；不存在 buy-only 或
  sell-only packet，不能据此推断方向。
- I081：45,612,932 message、51,183,942 entry；ordinary 26,415,345、derived
  24,768,597；mixed message 327,047。ordinary action 分布为 New 7,448,010、Change
  14,138,512、Delete 4,828,823；derived overlay 24,768,597。
- I081 中有 21,958 个 negative-price entry 与 384,777 个 zero-price entry，验证 signed/zero
  spread 不能当缺失。
- I083：86,617 message，其中 actual 586、trial 86,031、empty 23；trial best-bid market
  sentinel 479 次。该日 I083 没有 derived level，但 I081 derived overlay 很多，不能据此删字段。
- I025 9,870 条，其中 864 条 `SHOW-TIME != INFORMATION-TIME`；I030 1,360,652 条；
  I060 348,124、I064 12,990、I100 210、I140 日盘 199 条。
- I084 日盘 4,095,964 message，包含大量 O/S/P recovery snapshot；它说明 recovery 是常态
  数据链路的一部分，但不是同等数量的实时 market event。
- 当前 Aries conversion 发布 44,860,237 条 44-column Orderbook、4,892 条 basic-info；
  记录 3,721 个 gap event、448 个 symbol、7,660 个 missing sequence，均由 snapshot 标记恢复。

Orion SHM `to_csv` 对同日 DIFH6 输出 29,443 行。与 Aries 对照中 book/time/OHLC 可对齐，
但 Orion 对 I024 multi-item 会累计过量 volume，`trade_volume/action` 会粘在后续 book row，
且没有把 I084 statistics 正确合并为完整逐笔事实。这些是新设计分表和独立 Trade 的实证原因，
不是要求兼容 Orion CSV。

以上数字只绑定该文件、该 hash 与日盘 cutoff，不外推到夜盘或其他交易日。正式实现后必须
重新记录新 schema 的 row count、column count、hash、时间范围、gap/recovery/imputation
summary 与 storage cost。

## 当前 Aries 与 Orion 需要修正/有意不同之处

1. Aries `ProcessTrade()` 当前跳过 I024 `MATCH-TIME`，也不输出 item/continuation；新 Trade
   必须保存 `MATCH-TIME` 与 header time。
2. Aries I025 已解码 `SHOW-TIME` 但丢弃；新 HighLow 使用 SHOW-TIME。
3. Aries trial I024 会污染 last/pending volume；actual/trial state 必须分离。
4. Aries I081 row 可能继承旧 `match_flag`；新 `is_trial` 只来自当前 source。
5. Aries I083 无条件清空同一 book；必须分 actual/trial state。
6. Aries 开盘价恒取首个 actual item，不覆盖官方连续撮合开盘规则。
7. Aries I084 recovery merge 未逐字段遵守 snapshot as-of sequence；不能回滚较新 state。
8. Aries `orderbook_action` 丢 Change/Delete/Overlay；主表删除，delta 表保留 exact entry。
9. Aries derived-only I081 仍发布重复 ordinary row；分表后只发布实际变化的 record。
10. Aries `localtime=exchtime` 只是 offline fallback；新 manifest 必须显式记录。
11. Orion/Aries 都把 I024 state 折叠到下一条 book；新 schema 保留独立 event 和共同 sequence。
12. Orion `total_ask_volume/total_bid_volume` 实际装的是 I024 买卖成交 count，名称和语义错误；
    新 schema 只在 TradePacket 使用 official count 名称。
13. Orion `FutureOrderbook` 的 `open_interest` 来自盘后 I072，不进入逐笔 Orderbook。
14. 新 Aries 继续使用单进程同步 converter、Nova frontend options 与 Quill CSV writer，不依赖
    SHM，也不保留 legacy formatter。

## 后续实现顺序

用户确认字段后，按独立 L3 schema migration 实施：

1. 锁定 TAIFEX enums、POD `Orderbook<5>`、`Trade`、`TradePacket` 和 CSV header/precision。
2. 先建立 I024 multi-item/continuation、MATCH/INFORMATION time、trial、negative/zero spread、
   group 与 missing-volume 的失败测试。
3. 建立 I081 mixed action ordering、I083 actual/trial/empty/market sentinel 与 derived fixture。
4. 分离 actual/trial ordinary state、derived state、trade/group state 与 recovery state。
5. 实现 P0 writers，再按优先级增加 DerivedOrderbook、HighLow、OrderStats 与 status writer。
6. 创建新 schema/version、文件名和 generation manifest；不做 compatibility 双写，不覆盖旧文件。
7. 跑 focused CTest、完整 CTest、一天真实 full conversion 与独立全文件 scanner 对账。
8. 测量 row count、bytes、吞吐与压缩比后，再决定是否发布可选 Delta/underlying/session 表并
   重建全历史。

## 验收边界

- 每个 I024 price/quantity item 恰好一条 Trade，source key 唯一且 wire 顺序不变。
- first/continuation group 可识别；不生成 exchange 未提供的 order id 或 aggressor side。
- Trade `exchange_ns=MATCH-TIME`、`information_ns=INFORMATION-TIME`；HighLow 使用 SHOW-TIME。
- actual/trial ordinary book 独立，trial Trade/book 不改变 actual trade state。
- zero-price spread、empty level 与 trial market order 无歧义。
- I081 entry 严格按 source index 应用；ordinary/derived 更新都不丢失。
- TradePacket 最终 `exchange_total_volume` 与 source summary 相等，value 使用正确 multiplier，
  missing-volume 补值可由 summary/log 审计。
- I030、I024 counts、I072 open interest、I140 status 都位于正确 dataset，不再冒充 book field。
- gap/recovery/metadata/local-time fallback 由 date/symbol/sequence quality summary 审计；恢复
  snapshot 不被伪装成实时 event。

## 当前未实现边界

- 本文没有改变当前 `Orderbook<5>`、44/27 列 CSV、文件名或 `/tw_backup` 历史结果。
- 当前没有 Trade、TradePacket、DerivedOrderbook、OrderbookDelta、HighLow、OrderStats、
  QuoteRequest、underlying、status 或 session-statistics CSV。
- 当前没有 schema version/generation manifest；质量仍主要依赖逐日 converter log。
- 真实证据只覆盖 2026-08-03 futures 日盘；night session 与 options 需要独立设计和回归。
- continuation、无集合竞价开盘和不同 scope 状态的真实 fixture 仍需从更多日期抽取；官方协议
  已锁定实现责任，但不能用单日“未出现”宣称覆盖。
