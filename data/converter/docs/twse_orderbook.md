# TWSE / TPEx 股票 Orderbook 设计

更新时间：2026-08-03

状态：**Orderbook 字段已经锁定，尚未实现；Trade 字段仍待讨论**。当前代码与已发布文件
仍使用 `data/converter/docs/twse.md` 记录的 Orion-compatible 23 列 orderbook CSV。
本文是下一版股票 Orderbook、与 Trade 的事件边界及相关研究约束的唯一事实源。

## 结论

- TWSE / TPEx 与 TAIFEX 继续使用各自 exchange namespace 下的独立
  `Orderbook<N>`，不建立公共字段并集或数据基类。
- 股票 `Orderbook<N>` 是无构造、无默认成员初始化的 trivial、standard-layout POD-like
  record；`symbol` 使用 `char[16]`，盘口使用 C array。
- `Orderbook` 只在交易所消息实际携带普通盘口时发布。format 6/17 的中间
  trade-only 消息只生成 Trade，不复制撮合前盘口，也不提前使用撮合后最终盘口。
- static reference 放在 basic-info；逐次成交放在 Trade；文件级日期与 source format
  不在每条 Orderbook 重复。
- 三个协议状态 byte 分别保存为 `DisclosureState`、`LimitState`、`SessionState`，使用
  `uint8_t value + constexpr accessor`，不使用 C++ bitfield，也不再合并为
  `int64_t status`。
- CSV 直接输出三个 state 的 `value`，从 CSV 恢复 struct 后可直接使用 accessor；enum
  输出 underlying numeric value，不输出字符串。
- 本轮只固化文档，不修改 record、decoder、writer、测试或历史 CSV。完成 Trade 字段讨论
  后再统一实施。

## 范围与事实源

本设计交叉核对：

- `data/docs/exchange/TWSE集中市場即時交易資訊傳輸規格書(B.12.11)(202503)_20250113092444.pdf`
- `data/docs/exchange/上櫃股票IP行情網路規格書(V.12.16 TCPIP).pdf`
- Aries 当前 TWSE / TPEx protocol、basic-info、decoder、Orderbook 与 CSV writer
- `/home/liuxiang/dev/orion` 的 `StockDepth<5>`、TWSE converter、data engine、data
  reader 与 to-csv writer
- 2026-07-07 stock dump 与既有完整转换验证

本文只设计 converter 的日盘研究记录，不扩展当前处理的 format 范围，也不实施历史重建。

## 当前实现边界

当前 `aries::data::twse::Orderbook<N>` 仍为：

```text
std::string symbol
exchtime,localtime,status
last_price,previous_close,open,high_limit,low_limit,multiplier
total_volume,total_value,total_trade
ask_price[N],ask_volume[N],bid_price[N],bid_volume[N]
sequence
```

当前 CSV 仍是 Orion-compatible 23 列，固定输出 `symbol_id=-1`，丢弃 bid/ask volume，
并把 disclosure、limit 与 session 三个 byte 合并成十进制 `status`。下文全部是待实施的
下一版 contract，不能用来解释当前已发布 CSV。

## 最终字段

| 字段 | C++ 类型 | Orderbook CSV | 最终语义 |
|---|---|:---:|---|
| `symbol` | `char[16]` | 是 | 去除尾端空白并以 `\0` 结尾的交易所证券代码 |
| `exchange_ns` | `int64_t` | 是 | 触发该 Orderbook 的交易所业务事件时间，Unix epoch ns |
| `local_ns` | `int64_t` | 是 | 真实接收时间；offline 使用 exchange fallback |
| `symbol_id` | `int32_t` | 否 | 单次运行内 dense id，用于直接索引 basic-info/catalog |
| `market` | `Market` | 是，输出 underlying value | `1=TWSE`、`2=TPEX`，与 header service type 一致；同一日 CSV 可能同时包含两个 market |
| `disclosure` | `DisclosureState` | 是，输出 `value` | 原始 disclosure byte 与 named accessors |
| `limit_state` | `LimitState` | 是，输出 `value` | 原始 limit byte 与 named accessors |
| `session_state` | `SessionState` | 是，输出 `value` | 原始 session/status byte 与 named accessors |
| `last_price` | `double` | 是 | 截至本行最近一笔 actual 成交价 |
| `open` | `double` | 是 | 当日第一笔 actual 成交价；成交前为 0，不用 reference price 回填 |
| `high` | `double` | 是 | 截至本行的 actual 当日最高成交价 |
| `low` | `double` | 是 | 截至本行的 actual 当日最低成交价 |
| `total_volume` | `int64_t` | 是 | 交易所消息直接提供的 actual 累计成交量 |
| `total_value` | `double` | 是 | actual 累计成交额；缺失成交量按本文算法估算 |
| `ask_price[N]` | `double[N]` | 是，按档展开 | 本消息实际揭示的普通卖方第 1..N 档价格 |
| `ask_volume[N]` | `int64_t[N]` | 是，按档展开 | 本消息实际揭示的普通卖方第 1..N 档数量 |
| `bid_price[N]` | `double[N]` | 是，按档展开 | 本消息实际揭示的普通买方第 1..N 档价格 |
| `bid_volume[N]` | `int64_t[N]` | 是，按档展开 | 本消息实际揭示的普通买方第 1..N 档数量 |
| `source_sequence` | `uint64_t` | 是 | 交易所 header sequence；scope 由日期、market 与文件级 source format 限定 |

所有价格使用 `double`，所有 volume 使用有符号 `int64_t`。enum 和 state value 写 CSV 时
先转换为 `unsigned`，避免 `uint8_t` 被 formatter 当作字符。

## 最终 C++ schema

成员函数不增加对象存储，也不破坏 trivial/standard-layout；三个 state 可以保留完整的
`constexpr` accessor，但不得增加构造函数、virtual function 或默认成员初始化。

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aries::data::twse {

enum class Market : std::uint8_t {
  kTwse = 1,
  kTpex = 2,
};

enum class PriceLimitState : std::uint8_t {
  kNormal = 0,
  kDownLimit = 1,
  kUpLimit = 2,
  kReserved = 3,
};

enum class InstantaneousTrend : std::uint8_t {
  kNormal = 0,
  kHeldDown = 1,
  kHeldUp = 2,
  kReserved = 3,
};

enum class MatchingMethod : std::uint8_t {
  kCallAuction = 0,
  kContinuous = 1,
};

struct DisclosureState {
  std::uint8_t value;

  [[nodiscard]] constexpr bool has_trade() const noexcept {
    return (value & 0x80U) != 0;
  }

  [[nodiscard]] constexpr std::uint8_t bid_level_count() const noexcept {
    return static_cast<std::uint8_t>((value >> 4U) & 0x07U);
  }

  [[nodiscard]] constexpr std::uint8_t ask_level_count() const noexcept {
    return static_cast<std::uint8_t>((value >> 1U) & 0x07U);
  }

  // Format 6/17 only. Reserved in format 23.
  [[nodiscard]] constexpr bool disclosure_tag() const noexcept {
    return (value & 0x01U) != 0;
  }
};

struct LimitState {
  std::uint8_t value;

  [[nodiscard]] constexpr PriceLimitState trade_limit() const noexcept {
    return static_cast<PriceLimitState>((value >> 6U) & 0x03U);
  }

  [[nodiscard]] constexpr PriceLimitState best_bid_limit() const noexcept {
    return static_cast<PriceLimitState>((value >> 4U) & 0x03U);
  }

  [[nodiscard]] constexpr PriceLimitState best_ask_limit() const noexcept {
    return static_cast<PriceLimitState>((value >> 2U) & 0x03U);
  }

  [[nodiscard]] constexpr InstantaneousTrend
  instantaneous_trend() const noexcept {
    return static_cast<InstantaneousTrend>(value & 0x03U);
  }
};

struct SessionState {
  std::uint8_t value;

  [[nodiscard]] constexpr bool is_trial() const noexcept {
    return (value & 0x80U) != 0;
  }

  [[nodiscard]] constexpr bool is_delayed_open() const noexcept {
    return (value & 0x40U) != 0;
  }

  [[nodiscard]] constexpr bool is_delayed_close() const noexcept {
    return (value & 0x20U) != 0;
  }

  [[nodiscard]] constexpr MatchingMethod matching_method() const noexcept {
    return (value & 0x10U) != 0 ? MatchingMethod::kContinuous
                                : MatchingMethod::kCallAuction;
  }

  [[nodiscard]] constexpr bool is_opening() const noexcept {
    return (value & 0x08U) != 0;
  }

  [[nodiscard]] constexpr bool is_closing() const noexcept {
    return (value & 0x04U) != 0;
  }

  [[nodiscard]] constexpr std::uint8_t reserved() const noexcept {
    return static_cast<std::uint8_t>(value & 0x03U);
  }
};

static_assert(sizeof(DisclosureState) == 1);
static_assert(sizeof(LimitState) == 1);
static_assert(sizeof(SessionState) == 1);

template <std::size_t N>
struct Orderbook {
  static_assert(N > 0);

  char symbol[16];

  std::int64_t exchange_ns;
  std::int64_t local_ns;

  std::int32_t symbol_id;
  Market market;
  DisclosureState disclosure;
  LimitState limit_state;
  SessionState session_state;

  double last_price;
  double open;
  double high;
  double low;

  std::int64_t total_volume;
  double total_value;

  double ask_price[N];
  std::int64_t ask_volume[N];
  double bid_price[N];
  std::int64_t bid_volume[N];

  std::uint64_t source_sequence;
};

using Orderbook5 = Orderbook<5>;

static_assert(std::is_trivial_v<Orderbook5>);
static_assert(std::is_standard_layout_v<Orderbook5>);
static_assert(std::is_trivially_copyable_v<Orderbook5>);

}  // namespace aries::data::twse
```

对象创建不隐式清零；证券注册与 decoder 必须在首次读取或发布前写入全部有效字段。未使用
盘口尾档由 decoder 明确写为 0。结构不使用 `#pragma pack`，保持 `double` 与
`int64_t` 自然对齐。

## CSV contract

`Orderbook<5>` 的下一版 CSV 固定列顺序为：

```text
symbol,market,exchange_ns,local_ns,disclosure,limit_state,session_state,
last_price,open,high,low,total_volume,total_value,
ask_price1,ask_volume1,ask_price2,ask_volume2,ask_price3,ask_volume3,
ask_price4,ask_volume4,ask_price5,ask_volume5,
bid_price1,bid_volume1,bid_price2,bid_volume2,bid_price3,bid_volume3,
bid_price4,bid_volume4,bid_price5,bid_volume5,source_sequence
```

- `symbol_id` 只在内存中使用，不输出。
- `market` 输出 `Market` 的 underlying numeric value：`1=TWSE`、`2=TPEX`，不输出字符串。
- `disclosure`、`limit_state`、`session_state` 分别输出各自的 0..255 `value`，不展开
  accessor，也不重新组合为 packed `status`。
- CSV reader 将三个整数分别转换为 `uint8_t` 并写回 `.value`，随后直接调用同一组
  accessor。
- `trading_day` 由文件名/目录与同 generation basic-info 提供，不进入 Orderbook。
- `source_message` 由文件级 mode/manifest 确定，不进入每行。
- `event_type` 不持久化：Trade-only 只存在于 Trade；同时携带 Trade 与 Orderbook 的消息
  通过相同 `market + source_sequence` 关联。

## 时间 contract

- `exchange_ns` 使用 format 6/17/23 body 的撮合时间，不使用 dump 读取时间。
- 协议只有 microsecond 精度，转换为 ns 后低三位为 0。
- `local_ns` 的正式语义是真实接收/采集时间。当前 offline raw dump 没有 receive time，
  暂令 `local_ns = exchange_ns`，并在 manifest/log 记录
  `local_time_source=exchange_fallback`。
- fallback `local_ns` 不得用于延迟、排队或基础设施性能研究。

## actual 状态 contract

- `last_price/open/high/low/total_volume/total_value` 只由 actual 成交状态更新；trial 与
  held/status payload 不得污染。
- `last_price` 是截至当前 Orderbook 最近一笔 actual 成交价。
- `open` 是当日第一笔 actual 成交价；首笔成交前与全天无成交时均为 0，不使用
  `reference_price` 回填。
- `high/low` 按 actual Trade 实时更新，不能用日终值回填早期 Orderbook。
- `total_volume` 直接采用交易所消息的 actual 累计成交量，不通过本地 sum Trade 代替；
  sequence gap 后后续累计字段仍可恢复 quantity。

### `total_value`

TWSE format 6/17/23 不直接提供累计成交额。converter 对收到的 actual Trade 使用真实价格
累计；若交易所累计量差值大于收到的逐笔量，仅缺失部分使用最近 actual `last_price`
估算：

```text
volume_diff = exchange_total_volume - previous_total_volume
observed_volume = 当前对账区间收到的 actual trade volume 之和
observed_value = Σ(trade_price * trade_volume * effective_multiplier)
missing_volume = volume_diff - observed_volume

value_increment = observed_value
                + missing_volume * last_price * effective_multiplier
total_value += value_increment
```

- `observed_volume == volume_diff` 时，全部使用实际逐笔价格。
- `observed_volume < volume_diff` 时，只对差额 volume 使用最近 actual `last_price`。
- format 6/17 的 `effective_multiplier` 来自 basic-info；format 23 的 quantity 已是股数，
  `effective_multiplier=1`。
- 有缺失估算时按日期、market、symbol、source sequence、missing volume 与
  imputation price 写入质量日志，继续处理后续消息；不增加 `trade_value` 字段。
- BCD 范围、checksum、frame、sequence、重复消息和 catalog 完整性由协议/reader 层保证或
  记录，不在数值累加 hot path 重复做防御性判断。

## Orderbook 发布 contract

format 6/17 中，一张 incoming order 可能连续成交多次：

```text
trade-only
trade-only
...
trade + final Orderbook
```

发布规则为：

| source 消息 | Trade | Orderbook |
|---|:---:|:---:|
| trade-only | 是 | 否 |
| trade + 最终五档 | 是 | 是 |
| 仅普通五档 | 否 | 是 |

中间 trade-only 发生后真实盘口已经变化，但交易所没有揭示该中间状态：

- 不能复制撮合前 last-known book 生成新 Orderbook；该状态已经过期。
- 不能提前使用撮合完成后的 final book；这会引入未来信息。
- 可以在研究层根据成交与撮合前盘口生成明确标记的 inferred book，但不能把推断结果写成
  source Orderbook。
- 最后一条成交消息同时生成 Trade 和交易所实际揭示的 final Orderbook。
- format 23 的 disclosure bit 0 是 reserved，不能套用 format 6/17 的 match-group 边界。

普通盘口本身遵守：

- 空 level：`price=0, volume=0`。
- 市价委托：`price=0, volume>0`；不能只用 price 判断空档。
- 每条已发布 Orderbook 用本消息揭示内容替换全部普通 N 档，未使用尾档写 0。
- format 6/17 的 volume 单位为交易单位；format 23 的 volume 单位为股数。不同 source
  format 由文件级 contract 隔离，不能在同一累计序列中混用。

## State byte contract

三个 state 均保存原始 `value`，accessor 只做确定性的 mask/shift，不依赖编译器 bitfield
排列。CSV 也保存原始 value，因此 reserved bits 不丢失。

### `DisclosureState`

| bit | accessor | 语义 |
|---|---|---|
| 7 | `has_trade()` | 消息是否携带成交/试算价量 payload |
| 6-4 | `bid_level_count()` | bid 揭示档数，0..5 |
| 3-1 | `ask_level_count()` | ask 揭示档数，0..5 |
| 0 | `disclosure_tag()` | format 6/17：`1=trade-only`、`0=final book`；format 23 reserved |

### `LimitState`

| bit | accessor | 0 | 1 | 2 | 3 |
|---|---|---|---|---|---|
| 7-6 | `trade_limit()` | normal | down-limit | up-limit | reserved |
| 5-4 | `best_bid_limit()` | normal | down-limit | up-limit | reserved |
| 3-2 | `best_ask_limit()` | normal | down-limit | up-limit | reserved |
| 1-0 | `instantaneous_trend()` | normal | held-down | held-up | reserved |

`trade_limit` 描述当前消息中的成交，不是持续盘口属性；`best_bid_limit` 与
`best_ask_limit` 描述本消息揭示的最佳买卖价。具体 `high_limit/low_limit` 数值仍通过
`symbol_id` 查 basic-info catalog，CSV 研究按 `market + symbol` join basic-info。

held-down / held-up payload 中的价格是最近成交价、volume 为 0、时间是暂缓撮合起始时间；
它是状态事件，不生成 actual Trade，也不更新 actual OHLC、volume 或 value。

### `SessionState`

| bit | accessor | 语义 |
|---|---|---|
| 7 | `is_trial()` | `0=actual`、`1=trial` |
| 6 | `is_delayed_open()` | format 6/17 trial 的延后开盘标记；format 23 reserved |
| 5 | `is_delayed_close()` | format 6/17 trial 的延后收盘标记；format 23 reserved |
| 4 | `matching_method()` | `0=call-auction`、`1=continuous` |
| 3 | `is_opening()` | 开盘揭示 |
| 2 | `is_closing()` | 收盘揭示 |
| 1-0 | `reserved()` | 原样保存，不建立业务枚举 |

## Basic-info / builder state

以下字段不进入每条 Orderbook，以同 generation basic-info 为持久化事实源：

```text
trading_day
reference_price
high_limit
low_limit
multiplier
currency
```

- 内部 catalog 以 `(market, symbol)` 注册 dense `symbol_id`；Orderbook、后续 Trade 与
  basic-info state 对同一证券使用相同 id。
- `symbol_id` 不提供跨进程、跨日或跨文件稳定性，也不输出 CSV。
- 当日 CSV 可能同时包含 TWSE 与 TPEx，因此 `market` 仍是逐行字段；日期和 source
  format 在单一输出文件内不变化，因此留在 basic-info/文件 contract。

## high / low 与 snapshot

- format 6/17/23 的 normal actual Trade 实时更新 OHLC。
- TWSE format 12 / TPEx format 11 提供 symbol-level open/high/low/last/total volume，
  用作按消息时间和收盘结果的一致性检查；后到消息不静默覆盖早期状态。
- format 20/24 是五秒 snapshot，包含 open/high/low/last/total volume/五档，但最近成交
  价量只是瞬间采样，同一撮合时间不保证是最后一笔。
- 2026-07-07 dump 有 format 11/12，没有 format 20/24。snapshot 未来只能单独输出或用于
  recovery/validation，不能无标记混入 format 6/17 的逐次事件流。

## Trade：已知边界与待讨论项

状态：**字段尚未锁定，不在本轮实现**。后续讨论基于以下已经确认的 source 事实：

- format 6/17/23 每条消息最多携带一项成交或试算价量。
- format 6/17 同一 incoming order 可形成多条 trade-only，最后一条才携带撮合后的 final
  Orderbook；协议没有 order id，但可按 symbol 与 disclosure boundary 形成 match group。
- 每条 actual 成交都应生成 Trade；trade-only 不生成 Orderbook；最后一条同时生成 Trade
  与 Orderbook。
- raw Trade 不保存可确定性推导的 `trade_value`；需要时使用
  `price * volume * effective_multiplier`。
- held-down / held-up 的零量 payload 不是 actual Trade。
- aggressor side、order id、queue position 和 per-level order count 未由协议直接提供，
  不能写成 exchange fact。

format 6/17 的 match group 在连续、无 gap、具备撮合前完整五档时，可以研究推断同一
incoming order 的 `buy/sell/unknown`：成交路径消耗撮合前 ask 推断 buy，消耗撮合前 bid
推断 sell。同组 Trade 继承同一方向。集合竞价、trial、held、source gap、缺少撮合前
完整 book、超出五档或无法唯一对应时必须为 unknown。format 23 bit 0 reserved，在真实
odd-lot 数据验证前不能使用相同 group boundary。

若方向结论需要 final Orderbook 确认，必须记录结论实际可用的 source sequence，不能把
确认结果按较早 Trade 时间回填为当时已知信息，以免产生 look-ahead。具体 Trade 字段、
match id、方向字段与 CSV contract 留到下一轮逐项锁定。

## 研究衍生指标（deferred）

以下均不进入 raw Orderbook 或 Trade schema：

```text
trade_notional = price * volume * effective_multiplier
signed_trade_value = +trade_notional for inferred buy
signed_trade_value = -trade_notional for inferred sell

net_order_volume = after_volume - before_volume + executed_resting_volume
book_notional_flow = side_sign * price * net_order_volume * effective_multiplier
```

- unknown side 必须单独累计，不按 0 混入方向统计。
- 五档只能推导 price-level 净变化，不能恢复真实 New/Cancel order。
- before/after 必须按 price 对齐；第五档边界、市价档、gap、trial、集合竞价与 incomplete
  match group 需要明确排除或标记 unknown。
- currency 不同的 value 不经 FX conversion 不得合计。
- 窗口、标准化、level weighting、coverage 与是否进入因子库，待 source schema 完成后
  通过独立可复现实验决定。

## 不进入 Orderbook 的字段

| 字段 / 信息 | 去向 |
|---|---|
| `trading_day` | basic-info 与文件名/目录 |
| `reference_price`、`high_limit`、`low_limit`、`multiplier`、`currency` | basic-info/catalog |
| `trade_volume` / Orion `total_trade` | Trade |
| `source_message` | 文件级 mode/manifest |
| `event_type` | 由 Trade/Orderbook 两张表是否存在相同 source sequence 表达 |
| `average_ask_price` / `average_bid_price` | 研究层按需计算 |
| `total_ask_volume` / `total_bid_volume` | 由 N 档 volume 求和 |
| mid、spread、imbalance、microprice | factor/research 层 |
| delta volume/value | 连续行可推导，gap 时结合质量日志 |
| format 19 lifecycle 状态 | 独立 `InstrumentStatus` |
| format 2/4 市场统计 | market statistics |
| order count、order id、queue position | 协议未提供，不伪造 |

## 相对当前实现与 Orion 的有意差异

1. `std::string` 改为 `char[16]`；Orderbook 使用 raw C arrays、无默认初始化，并通过
   trivial/standard-layout static assertions 锁定 POD-like 属性。
2. 内部保存真实 dense `symbol_id`，CSV 不再输出固定 `-1`。
3. `market` 成为 Orderbook 与 CSV 的 numeric 字段；当前 decoder 只按 symbol 建 state 的
   行为必须改为 `(market, symbol)`。
4. packed `int64_t status` 拆为三个 1-byte state；CSV 输出三个原始 value，而非展开列或
   重新打包整数。
5. reference、涨跌停价和 multiplier 不在每条 Orderbook 重复，通过 catalog/basic-info
   关联。
6. `total_trade/trade_volume` 从 Orderbook 删除，逐次成交进入 Trade。
7. actual last/OHLC/volume/value 不被 trial 或 held payload 污染；open 不使用 reference
   price 回填。
8. 股票 CSV 首次完整输出 bid/ask volume，并增加 actual high/low。
9. trade-only 不再清空盘口，也不复制 last-known book 生成 Orderbook；只有 source 实际
   携带五档才发布。
10. `total_value` 保留实际逐笔价格，只有累计量差额使用 last price 估算，并记录问题。

## 后续实施顺序

1. 下一轮逐项锁定 Trade POD struct、CSV、match group 与 trial/status 边界。
2. Trade 完成后为新 Orderbook/Trade schema 建立独立 L3 migration plan、schema version
   与 generation manifest。
3. 先建立 POD layout、CSV round-trip、state value、actual/trial、held、trade-only/final
   book、empty/market level、regular/odd-lot unit 与 total-value gap 算法 fixtures。
4. 分离 internal `StockState`、basic-info catalog 与 emitted records，修正 decoder 状态机。
5. 增加 format 11/12 validation，运行 focused/full CTest 和真实日全量转换。
6. 测量新 CSV/Trade 的大小、吞吐和压缩比后再决定长期存储格式与历史重建计划。

## 当前未实现边界

- 当前 23 列 CSV、C++ struct、文件名和批量重建结果均未因本文改变。
- Trade schema 尚未锁定，因此不能开始 Orderbook/Trade 联合实现。
- 2026-07-07 dump 没有 format 17/23/20/24；warrant、odd-lot 与 snapshot 的真实数据回归
  仍需其他日期或采集源。
- generation manifest/schema version 尚未建立；`local_ns` fallback 当前只在文档说明。
- TAIFEX 期货专属字段不在本文定义；其事实源为
  `data/converter/docs/taifex_orderbook_trade.md`。
