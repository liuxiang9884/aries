# TWSE / TPEx 股票 Orderbook Contract

更新时间：2026-08-04

状态：已实现 `twse-orderbook-v2`，本文件是股票 normalized Orderbook、撮合分组、方向、
累计量额和 CSV schema 的唯一事实源。

## 定位与范围

- TWSE / TPEx 使用股票专属的 `aries::data::twse::Orderbook<N>`；不与 TAIFEX
  Orderbook 建公共字段并集或数据基类。
- 一条 normalized Orderbook 表示 converter 已收到完整边界、可交给策略消费的一次
  `(market, symbol)` 市场状态更新，不等同于一条交易所 wire message。
- format 6 / 17 的同一 incoming order 可能以多条 trade-only 分片加一条 terminal
  message 传输。converter 只在 terminal 到达后发布一条原子事件。
- 不再生成独立股票 Trade record 或 Trade CSV。完整撮合组的方向、成交项数和成交量直接
  保存在 terminal Orderbook 中。
- raw dump、文件名和 basic-info 提供 trading day 与静态证券资料；它们不在每行
  Orderbook 重复。

协议事实源：

- `data/docs/exchange/TWSE集中市場即時交易資訊傳輸規格書(B.12.11)(202503)_20250113092444.pdf`
- `data/docs/exchange/上櫃股票IP行情網路規格書(V.12.16 TCPIP).pdf`
- `data/docs/converter/twse.md`

## C++ schema

```cpp
enum class Market : std::uint8_t {
  kTwse = 1,
  kTpex = 2,
};

enum class TradeSide : std::uint8_t {
  kUnknown = 0,
  kBuy = 1,
  kSell = 2,
};

template <std::size_t N>
struct Orderbook {
  char symbol[16];

  std::int64_t exchange_ns;
  std::int64_t local_ns;

  std::int32_t symbol_id;

  Market market;
  DisclosureState disclosure;
  LimitState limit_state;
  SessionState session_state;

  TradeSide trade_side;
  std::uint32_t trade_count;

  double last_price;
  double open;
  double high;
  double low;

  std::int64_t trade_volume;
  std::int64_t total_volume;
  double total_value;

  double ask_price[N];
  std::int64_t ask_volume[N];
  double bid_price[N];
  std::int64_t bid_volume[N];

  std::uint64_t source_sequence;
};
```

- record 无构造函数、默认成员初始化和 virtual function；`symbol` 与盘口都使用 raw
  C array。
- `Orderbook<5>` 必须是 trivial、standard-layout、trivially-copyable。
- 不使用 `#pragma pack`。当前 Linux x86-64 自然对齐 ABI 的 `sizeof(Orderbook<5>)`
  为 272、`alignof` 为 8；这个大小不是跨平台 wire ABI。
- 所有 price 和 `total_value` 使用 `double`；所有 volume 使用有符号 `int64_t`。
- decoder 在每条发布路径写满全部字段；未揭示尾档写 `price=0, volume=0`。

## 字段语义

| 字段 | CSV | 语义 |
|---|:---:|---|
| `symbol` | 是 | 去除尾端空白、以 `\0` 结尾的证券代码 |
| `market` | 是 | `1=TWSE`、`2=TPEX`；身份键的一部分 |
| `exchange_ns` | 是 | terminal message 的撮合时间，UTC epoch ns |
| `local_ns` | 是 | terminal 到达并使事件完整可用的本地接收时间；offline 使用 exchange fallback |
| `symbol_id` | 否 | 单次运行内 dense id，用于直接索引 internal catalog；不跨日稳定 |
| `disclosure` | 是 | terminal format 的原始 `data_flag` byte |
| `limit_state` | 是 | terminal format 的原始 `limit_flag` byte |
| `session_state` | 是 | terminal format 的原始 `status` byte |
| `trade_side` | 是 | 完整 group 的推断主动方向；不可唯一推断为 0 |
| `trade_count` | 是 | group 内 actual 成交价量 payload 数，不是对手方订单数 |
| `last_price` | 是 | 应用完整 group 后最近一笔 actual 成交价 |
| `open/high/low` | 是 | 按当日每一笔 actual 成交价更新，不使用 trial 或 held payload |
| `trade_volume` | 是 | group 内 actual 成交量之和；book-only 为 0 |
| `total_volume` | 是 | terminal message 的交易所 actual 累计量 |
| `total_value` | 是 | 本地按实际成交和缺失量补值算法得到的 actual 累计成交额 |
| `ask/bid_price[N]` | 是 | terminal 普通盘口；空档为 0，市价档可能 `price=0, volume>0` |
| `ask/bid_volume[N]` | 是 | terminal 普通盘口数量 |
| `source_sequence` | 是 | 使事件完整可用的 terminal header sequence |

以下信息不进入逐行 Orderbook：

```text
trading_day
reference_price
high_limit
low_limit
multiplier
currency
source format
event_type
match_group_id
```

`trading_day` 和 source format 属于文件/运行 contract；静态价格、multiplier 和 currency
通过同 generation basic-info 按 `(trading_day, market, symbol)` 关联。Orderbook 只在
terminal 发布，因此不需要额外 group id 或 delayed-side sequence。

## State byte

三个状态都保存 `uint8_t value`，accessor 只做显式 mask/shift。CSV 直接输出 0..255 的
原始 value，不展开位域，也不重新组合 packed `status`。

### DisclosureState

| bit | accessor | 语义 |
|---|---|---|
| 7 | `has_trade()` | 消息携带成交/试算价量 payload |
| 6-4 | `bid_level_count()` | bid 揭示档数 0..5 |
| 3-1 | `ask_level_count()` | ask 揭示档数 0..5 |
| 0 | `disclosure_tag()` | format 6/17：`1=trade-only`、`0=terminal book`；format 23 reserved |

### LimitState

| bit | accessor | `0` | `1` | `2` | `3` |
|---|---|---|---|---|---|
| 7-6 | `trade_limit()` | normal | down-limit | up-limit | reserved |
| 5-4 | `best_bid_limit()` | normal | down-limit | up-limit | reserved |
| 3-2 | `best_ask_limit()` | normal | down-limit | up-limit | reserved |
| 1-0 | `instantaneous_trend()` | normal | held-down | held-up | reserved |

`trade_limit` 描述本消息成交；涨跌停数值来自 basic-info。held payload 中的零量价不是
actual Trade，不更新 OHLC、volume 或 value。

### SessionState

| bit | accessor | 语义 |
|---|---|---|
| 7 | `is_trial()` | `0=actual`、`1=trial` |
| 6 | `is_delayed_open()` | format 6/17 延后开盘；format 23 reserved |
| 5 | `is_delayed_close()` | format 6/17 延后收盘；format 23 reserved |
| 4 | `matching_method()` | `0=call auction`、`1=continuous` |
| 3 | `is_opening()` | 开盘揭示 |
| 2 | `is_closing()` | 收盘揭示 |
| 1-0 | `reserved()` | 原样保留 |

## Match-group 状态机

builder 按 `(market, symbol)` 维护独立 `StockState`，将 actual OHLC/value、最后完整盘口、
metadata、pending group 和 emitted record 分开。其他证券的消息不会结束当前证券的 group。

| 输入 | 行为 | 发布 |
|---|---|---|
| book-only，无 pending | 替换完整 N 档，成交增量清零 | 立即一条 |
| single trade + final book | 应用成交、累计量额和 final book | 一条 |
| trade-only | 保存 pre-match book，累计 actual price/volume/count/value | 不发布 |
| 后续 trade-only | 累加同一时间的 pending group | 不发布 |
| trade + final book | 应用最后成交与 final book，关闭 group | 整组一条 |
| held-down/up | payload 不算成交；关闭 pending，清空盘口并标记 invalid | 一条 held event |
| trial | 不更新 actual state、盘口或 pending group | 不发布 |
| format 23 | bit 0 忽略；一条 source message 作为一个 event | 一条 |

normal terminal 后的盘口是交易所实际揭示的撮合终局。中间 trade-only 后真实盘口已经变化，
因此不能发布撮合前 stale book，也不能提前使用尚未到达的 final book。

held 必须立即通知实盘消费者：该行保留 pending group 的成交聚合和 terminal state，但全部
N 档明确写 0；internal book 变为 invalid。只有后续实际携带完整盘口的 book-only 或
final-book message 才恢复 valid。

format 6/17 同组 actual trade 必须具有相同 source format 和
`exchange_ns`。sequence 连续性按
`(trading_day, market, source format)` 审计，不能按 symbol 判断，因为证券共享 source
sequence。不同 source format、不同时间、gap、frame corruption 或 EOF
导致未闭合时：

- 记录 day、market、symbol、source format、first/last sequence、trade count/volume
  和原因；
- 不发布伪装成完整事件的 group；
- 保留已观察成交到 value reconciliation interval，防止后续 terminal 重复累计；
- 旧盘口标记 invalid，后续完整 terminal 才重新锚定；
- 其他证券继续处理，整日状态为 `published_partial`。

## Trade side

交易所没有直接提供 aggressor side，输出必须允许 `unknown`：

1. group 开始前保存最后一个 valid pre-match book。
2. 只有 continuous、非 opening/closing/trial、无 gap、best bid/ask 都是正价且盘口未
   locked/crossed 时才推断。
3. 第一笔实际成交价等于或穿过 pre-match ask，且不能同时属于 sell 条件时为 buy；等于
   或穿过 pre-match bid，且不能同时属于 buy 条件时为 sell。
4. 同一 group 的所有成交共享该方向；terminal book 不能回填成多个较早事件。
5. 市价档、缺少一侧、locked/crossed、集合竞价、开收盘、无 valid pre-book 或无法唯一
   判断时为 unknown。
6. held payload 不参与推断；held-ended group 仍可用 held 前的 actual trade path 和
   pre-match book 推断。
7. format 23 的 bit 0 是 reserved，真实 odd-lot 回归完成前方向固定 unknown。

方向只使用 terminal 到达时已经可得的信息，不产生 look-ahead。signed money flow、盘口
flow、imbalance 等属于后续研究层，不进入 raw normalized schema。

## Volume 与 value

- format 6/17 的 volume 单位为交易单位，`effective_multiplier` 来自同 market/symbol
  的 format 1 basic-info。
- format 23 的 volume 已是股数，`effective_multiplier=1`。
- converter 在最近一次 accepted exchange cumulative volume 与当前 terminal 之间保存
  observed actual volume/value：

```text
observed_value = Σ(trade_price * trade_volume * effective_multiplier)
volume_diff = terminal_total_volume - previous_accepted_total_volume
missing_volume = volume_diff - observed_volume

total_value += observed_value
             + missing_volume * last_price * effective_multiplier
```

`missing_volume=0` 时全部使用逐笔真实成交价；大于 0 时仅缺失部分使用当前 actual
`last_price`，并记录 market、symbol、sequence、volume diff、observed/missing volume
和 imputation price。terminal 对账后才推进 cumulative checkpoint 并清空 interval。

trial 与 held payload 本身不增加 actual count/volume/value。book-only 若 exchange
累计量增长，也使用同一补值规则，但本行 `trade_count=0, trade_volume=0`。

## CSV contract

schema 名称：`twse-orderbook-v2`。`Orderbook<5>` 固定为 37 列：

```text
symbol,market,exchange_ns,local_ns,
disclosure,limit_state,session_state,trade_side,trade_count,
last_price,open,high,low,trade_volume,total_volume,total_value,
ask_price1,ask_volume1,ask_price2,ask_volume2,ask_price3,ask_volume3,
ask_price4,ask_volume4,ask_price5,ask_volume5,
bid_price1,bid_volume1,bid_price2,bid_volume2,bid_price3,bid_volume3,
bid_price4,bid_volume4,bid_price5,bid_volume5,source_sequence
```

- enum/state 输出 underlying numeric value；`symbol_id` 不输出。
- price 与 `total_value` 固定 4 位小数；volume/count/time/sequence 输出整数。
- 使用 `quill::CsvWriter` compile-time schema，由 Nova 管理 backend 生命周期。
- v2 直接替换 23 列 Orion legacy schema，不保留 legacy writer、双写开关或兼容 reader；
  consumer 必须通过 exact header 判断 schema，不能混读两代文件。
- orderbook 与 30 列 basic-info 继续 staged 写入并成对发布；`--overwrite` 失败时恢复旧
  generation。

## 2026-07-07 真实验证

输入：`/tw_backup/data/tw/raw/stock/twse_stock_20260707.dump`

全市场 `all --dry-run`：

```text
file_bytes=3153093917
messages=25993761
source_actual_trades=2312789
match_groups=2254614
multi_trade_groups=45270
trades_in_multi_groups=103445
held_ended_groups=78
buy_groups=1106454
sell_groups=1109972
unknown_groups=38188
sequence_gaps=0
incomplete_groups=0
value_imputations=0
```

`stock` 完整 CSV 验证：

```text
orderbook_rows=15548031
orderbook_bytes=3705213634
basic_info_rows=40841
basic_info_bytes=5118361
actual_trades=1906696
match_groups=1855576
multi_trade_groups=39238
trades_in_multi_groups=90358
held_ended_groups=78
buy_groups=830541
sell_groups=987604
unknown_groups=37431
wall_time_seconds=18.92
max_rss_kib=348116
```

独立逐行扫描确认 15,548,031 行全部为 37 列，`local_ns=exchange_ns`、三个 state 与
direction 在范围内、无累计量/额回退、每个 market 的 source sequence 严格递增；266 条
held state 行的全部盘口为 0。聚合得到的 group/trade/side 计数与 converter summary 一致，
所有 decimal 字段固定 4 位。basic-info 与迁移前文件 SHA-256 相同：
`093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a`。
orderbook v2 SHA-256 为
`28adc5806ce2a8315b4927e94af85812924afc3b807a719cc3dcf72ffd3428be`。

真实验证产物位于 `/home/liuxiang/tmp`，不进入 git。该日没有 format 17/23，warrant 与
odd-lot 路径仍由 synthetic fixture 覆盖。

## 相对 Orion / legacy 的有意差异

- 一个 normalized event 对应完整 match group，不按 wire trade fragment 逐行输出。
- 股票不再有独立 Trade CSV；成交方向、数量和项数位于 terminal Orderbook。
- `std::string` 改为 `char[16]`；内部 dense `symbol_id` 不写 CSV。
- market 成为 numeric row field；state 从 packed integer 拆成三个原始 byte。
- 新增 high/low、trade side/count/volume 和全部 bid/ask volume。
- 删除 previous close、涨跌停价和 multiplier 的逐行重复；它们由 basic-info 提供。
- actual open/high/low/value 不受 trial 或 held payload 污染。
- `local_ns` 表示 terminal 可用时间；offline 明确标记 exchange fallback。
- price/value 从 legacy 2 位改为协议精度对应的 4 位。
- 增加 group、side、held、gap、incomplete 和 missing-volume 质量统计与逐项日志。

## 仍未覆盖的边界

- 当前 converter 是同步 offline dump 工具；实时调用方需要把真实 receive timestamp 传给
  同一 decoder API，不能使用 offline fallback 做 latency 研究。
- format 20/24 snapshot、format 19 lifecycle 与 format 2/4 市场统计不混入本表。
- 五档只能支持 price-level 净变化研究，不能恢复 order id、queue position 或完整挂撤单。
- historical `/tw_backup` 文件只有经 v2 converter 重建后才符合本文 contract；exact header
  不匹配的 23 列文件仍是 legacy generation。
