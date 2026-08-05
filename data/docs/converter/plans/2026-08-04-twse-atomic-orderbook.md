# TWSE / TPEx 原子 Orderbook 实施计划

日期：2026-08-04

状态：已合并到 `main`；实现、单日验证与首批五个自然日重建完成，
其余历史 `/tw_backup` v2 重建待首批问题审查后决定

实施结果：代码、focused/full/sanitizer tests、37 列 Quill CSV、结构化质量日志和
专题文档已完成；2026-07-07 `all --dry-run` 与独立基线一致，`stock`
完整 CSV 已逐行验证。详细结果见
`data/docs/converter/twse_orderbook.md` 与 `data/docs/converter/testing.md`。
实现提交为 `5087703`。

## 目标

- 将 TWSE / TPEx format 6/17 的一组撮合传输分片合并为一条策略可消费的原子
  `Orderbook<N>`，不把单条 trade-only wire message 当作独立市场事件发布。
- 移除独立 Trade record / CSV 方案；在 Orderbook 中保存完整 match group 的
  `trade_side`、`trade_count` 与 `trade_volume`。
- 实时与 offline converter 使用同一状态机语义：只有 terminal message 到达后才发布；
  `local_ns` 表示事件完整可用的时间，而不是第一条分片到达的时间。
- 用新的 37 列研究 CSV 替换 Orion-compatible 23 列 legacy schema，不保留双轨兼容。
- 使用单元 fixture 和 2026-07-07 全日真实 dump 验证 grouping、时间、held、方向、
  volume/value、CSV 与 best-effort publication。

## 已锁定 contract

### 记录定位

`Orderbook<N>` 不再表示“一条 wire message 解出的盘口”，而表示“converter 已收到完整
边界、可以交给策略消费的一次 symbol-level 市场状态更新”。

- format 6/17 的 trade-only 分片只进入内部 buffer，不单独 callback 或写 CSV。
- `trade + final book` 结束正常 match group，并发布一条带最终盘口的 Orderbook。
- held-down / held-up 结束 match group，并立即发布一条盘口不可用的 held Orderbook；
  不能沿用撮合前盘口，也不能等待约两分钟后的恢复消息才通知实盘消费者。
- book-only 消息没有待决 group 时立即发布，`trade_count=0`、`trade_volume=0`、
  `trade_side=unknown`。
- 其他 symbol 的消息可以穿插；group builder 必须按 `(market, symbol)` 独立维护状态。
- trial payload 不发布，也不修改 actual OHLC、volume、value 或 pending group。
- format 23 的 disclosure bit 0 是 reserved，不套用 format 6/17 的 group boundary；
  当前按一条 source message 一个 event 处理，真实 odd-lot 数据回归完成前方向保守为
  `unknown`。

### 最终 POD schema

股票继续使用独立于 TAIFEX 的 namespace 与类型。所有 volume 保持 `int64_t`；struct
无构造函数、无默认成员初始化、无 virtual function，不使用 `#pragma pack`。

```cpp
enum class TradeSide : std::uint8_t {
  kUnknown = 0,
  kBuy = 1,
  kSell = 2,
};

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

在当前 Linux x86-64 自然对齐 ABI 下，`sizeof(Orderbook<5>) == 272`、`alignof == 8`。
保留 trivial、standard-layout 与 trivially-copyable compile-time assertions；不把具体
`sizeof` 作为跨平台 wire ABI。

### 字段语义

- `exchange_ns`：完整 match group 共同的交易所撮合时间。format 6/17 group 内所有
  actual trade 必须相同；协议为 microsecond 精度，转换后 ns 低三位为 0。
- `local_ns`：terminal final-book 或 held message 的真实接收时间；offline dump 没有
  receive timestamp，使用 `exchange_ns` fallback 并在日志标记。
- `symbol_id`：单次运行内 dense id，只用于直接索引 builder/catalog，不输出 CSV。
- `market`：`1=TWSE`、`2=TPEX`，与 header service type 一致。
- `disclosure`、`limit_state`、`session_state`：terminal message 的三个原始 state byte；
  accessor 与已锁定设计一致。
- `trade_side`：当前完整 match group 的推断主动方向；book-only 和无法唯一推断时为
  `unknown`。
- `trade_count`：group 内 actual trade 价量记录数；不等同于对手方订单数。
- `trade_volume`：group 内 actual trade volume 之和；book-only 为 0。
- `last_price`：应用完整 group 后的最后一笔 actual 成交价；book-only 沿用 actual state。
- `open/high/low`：按 group 内每一笔 actual price 更新，不用 terminal last price 代替整组
  价格路径。
- `total_volume`：terminal message 提供的交易所 actual 累计量。
- `total_value`：应用完整 group 后的 actual 累计成交额；不增加 `trade_value` 字段。
- `source_sequence`：使本条 event 完整可用的 terminal header sequence。
- decoder API 显式接收当前 source message 的 `local_ns`；实时调用方传真实 receive time，
  offline dump converter 传该消息的 `exchange_ns` fallback。不得在 builder 内读取 wall clock。
- 正常 final/book-only event 的 bid/ask 来自 terminal message；held event 的全部 level
  明确写 0，`limit_state.instantaneous_trend()` 表示盘口不可用。

### CSV contract

下一版 `Orderbook<5>` CSV 固定为 37 列：

```text
symbol,market,exchange_ns,local_ns,
disclosure,limit_state,session_state,trade_side,trade_count,
last_price,open,high,low,trade_volume,total_volume,total_value,
ask_price1,ask_volume1,ask_price2,ask_volume2,ask_price3,ask_volume3,
ask_price4,ask_volume4,ask_price5,ask_volume5,
bid_price1,bid_volume1,bid_price2,bid_volume2,bid_price3,bid_volume3,
bid_price4,bid_volume4,bid_price5,bid_volume5,source_sequence
```

- enum 与三个 state 直接输出 underlying numeric value，不输出字符串或展开位域。
- `symbol_id`、`trading_day`、source format、multiplier、currency、reference/high/low limit
  不进入逐行 CSV；分别属于 internal catalog、文件 contract 或 basic-info。
- price 与 `total_value` 使用固定 4 位小数，匹配交易所 price 的 4 位小数精度；测试锁定
  header、列顺序和数值格式。
- schema 名称为 `twse-orderbook-v2`，以 exact header 作为文件内 discriminator；输出路径
  沿用现有命名，旧 23 列文件由 rebuild 原子替换，不保留 legacy writer 或兼容 reader。
- basic-info 30 列 schema 与双文件 staged publication contract 不变。

## Match-group 状态机

每个 `(market, symbol)` 维护独立 `StockState`，将持久 actual state、最后有效盘口、
pending match group 和 basic-info reference 分离。不得继续把 emitted `Orderbook` 本身同时
当作全部 mutable builder state。

| 当前输入 | pending group | 行为 | 发布 |
|---|---|---|---|
| book-only | 无 | 替换完整 N 档，trade 增量清零 | 立即发布 |
| single trade + final book | 无 | 建立并立即终止 count=1 group，更新 actual 与最终盘口 | 一条 |
| trade-only | 无 | 保存 pre-match book，建立 group，累计实际价量 | 不发布 |
| trade-only | 有 | 累加同 group 的 price、volume、count、value | 不发布 |
| trade + final book | 有 | 累加最后一笔，写入 terminal state 与最终盘口 | 一条 |
| held-down/up | 有 | 不把 held payload 当 Trade；使用 terminal state，清空盘口 | 一条 held event |
| trial | 任意 | 不更新 actual state；不改变或发布 pending actual group | 不发布 |
| 其他 symbol 消息 | 任意 | 只处理其自身 state；不关闭当前 symbol group | 按该 symbol 规则 |

format 6/17 group 的所有 actual trade 必须具有相同 source format 和
`exchange_ns`。若同一 symbol 在 terminal 前出现不同 source format、不同撮合时间、
无法解释的 actual/session 状态、sequence gap 或 EOF：

1. 记录 `incomplete_match_group`，包括 day、market、symbol、first/last/observed sequence、
   trade count/volume 和原因；
2. 不把不完整 group 伪装为完整 Orderbook；
3. 已观察的实际价量留在累计 value 对账区间，后续完整 terminal book 可重新锚定盘口；
4. 当日输出标记为 `published_partial`，其他 symbol 继续处理。

sequence 连续性不按 symbol 检查，因为不同证券共享 source sequence。auditor 的 key 固定为
`(trading_day, market, source format)`；pending group 只引用 auditor 的 gap 状态以及自身
first/last/terminal sequence。不同 source format 的 sequence 不互相比较。

held event 发布后，该 symbol 的 internal book 标记为 invalid。后续第一条实际携带完整盘口的
book-only 或 final-book event 才恢复 valid；在此之前实盘 consumer 不得使用旧盘口。

## Trade side 推断

方向不是交易所直接字段，必须允许 `unknown`。推断只使用 event 发布时已经可得的信息：

1. group 开始前保存最后一个 valid、完整的 pre-match N 档。
2. 仅在 normal continuous matching、非 opening/closing/trial、无 gap 且 best bid/ask
   均可判定时推断；terminal held 状态本身不否定此前 actual group 的方向。
3. 第一笔 actual price 只能对应或穿过 pre-match ask 时为 `buy`；只能对应或穿过 pre-match
   bid 时为 `sell`；locked/crossed、market-price level、两侧均可能、两侧均不匹配或缺少
   五档时为 `unknown`。
4. 同一 group 的全部价量共享一个 side；后续 trade 分片不得把一个 group 拆成多个方向。
5. terminal final book 只用于一致性检查；不得用无法唯一解释的 book delta 强行制造方向。
6. held payload 本身不参与方向推断；held-ended group 仍可使用 pre-match book 与此前
   actual trade path 推断。没有 valid pre-match book 时保持 unknown。
7. Orderbook 只在 terminal 到达后发布，因此不需要 `match_group_id` 或
   `side_available_sequence`，也不会把终局信息回填到已发布的早期行。

方向实现单独放在小型纯函数/组件中，输入明确的 pre-book、session state 与 group price
路径，便于 synthetic fixture 和后续替换算法，不与 CSV writer 耦合。

## Volume / value contract

- format 6/17 volume 单位保持交易单位，`effective_multiplier` 从同 market/symbol 的
  format 1 basic-info 获取。
- format 23 volume 保持股数，`effective_multiplier=1`；全部 volume 继续使用
  `int64_t`，不按普通交易的 8-digit wire 范围缩窄。
- 正常完整 group：

```text
trade_count = actual trade payload count
trade_volume = sum(actual trade payload volume)
observed_volume = sum(actual trade volume since previous checkpoint)
observed_value = sum(actual price * volume * effective_multiplier since previous checkpoint)
volume_diff = terminal_total_volume - previous_accepted_total_volume
missing_volume = volume_diff - observed_volume
total_value += observed_value
             + missing_volume * last_price * effective_multiplier
```

- `missing_volume == 0` 时完整使用真实逐笔价格；大于 0 时仅缺失部分使用 terminal
  `last_price`，并写质量日志。
- book-only 的 exchange total volume 如相对 builder state 增长，按同一 missing-volume
  规则补值并记录，`trade_count/trade_volume` 仍为 0。
- value reconciliation 以最近一次已接受的 exchange cumulative volume 为 checkpoint，并在
  checkpoint 之间保存所有已观察的 actual volume/value；因此未发布的 incomplete group 不会
  在下一次完整 terminal 到达时被重复累计。terminal 对账成功后才推进 checkpoint 并清空
  interval accumulator。
- trial 与 held payload 本身不增加 actual count、volume 或 value；held 行携带的是此前
  pending group 已累计的增量。
- 价格/BCD/frame/sequence 合法性由协议 reader 保证；数值 hot path 不重复增加与协议性质
  无关的防御性范围判断。

## 影响文件

主要实现：

- `data/converter/twse/orderbook.h`
- `data/converter/twse/message_decoder.h`
- `data/converter/twse/message_decoder.cpp`
- `data/converter/twse/csv_writer.cpp`
- `data/converter/twse/dump_converter.h`
- `data/converter/twse/dump_converter.cpp`
- `data/converter/twse/twse_dump_converter_main.cpp`

测试与 fixture：

- `tests/data/converter/twse/test_message_builder.h`
- `tests/data/converter/twse/message_decoder_test.cpp`
- `tests/data/converter/twse/dump_converter_test.cpp`

文档：

- `data/docs/converter/twse_orderbook.md`
- `data/docs/converter/twse.md`
- `data/docs/converter/testing.md`
- `data/docs/converter/onboarding.md`
- 必要时同步根目录 `docs/onboarding.md` 的当前主线，不复制模块细节。

## 非目标

- 不修改 TAIFEX Orderbook / Trade schema。
- 不实现夜盘、format 20/24 snapshot 输出、format 19 lifecycle 表或 format 2/4 市场统计。
- 不恢复 order id、queue position、per-level order count、挂单或撤单对象。
- 不把 signed money flow、book flow、imbalance 或其他 factor 写入 raw Orderbook。
- 不保留 Orion 23 列 writer、legacy struct、双写开关或兼容 reader。
- 不因本任务引入 SIMD、压缩格式、数据库或新依赖。
- 不新增 sidecar manifest；schema 由 exact header，generation 由 trading day、mode、converter
  commit 与 runner summary/log 共同记录。

## 实施阶段

### 阶段 1：先建立失败 fixture 与 schema contract

1. 扩展 test message builder，使 fixture 能显式设置 disclosure bit、trial、held、market、
   多条 trade price/volume 和交错 symbol sequence。
2. 添加 `TradeSide`、typed states 与最终 `Orderbook<5>` 的 compile-time trait/layout 测试。
3. 先写失败测试锁定 37 列 CSV header、raw state numeric 输出、bid/ask volume、4 位小数和
   `symbol_id` 不输出。
4. 保留 basic-info 既有 fixture，确认本迁移不改变其 30 列 bytes。

### 阶段 2：拆分 builder state 并实现原子 grouping

1. 将当前 `unordered_map<string, Orderbook<5>>` 改为按 `(market, symbol)` 索引的
   `StockState`；由共享 internal catalog 注册 dense `symbol_id`。basic-info record/CSV 不新增
   `symbol_id`，decoder 通过同一 catalog index 查对应 metadata。
2. 将 wire decode 拆为 header/state、可选 actual trade、可选普通 N 档三个部分，避免在
   trade-only 上清空 last-known book。
3. 实现 single、multi、interleaved-symbol、final-book 与 held terminal 状态机；
   `Process()` 只在一个完整 event 可发布时返回 record。
4. 在所有发布路径显式写满 POD 的有效字段和未使用尾档，不依赖默认初始化。
5. 添加 pending group EOF/gap/anomaly 的统计与日志接口，保持其他 symbol best-effort。

### 阶段 3：实现 actual state、value 与方向

1. 按 group 内每一笔真实价格更新 last/OHLC、trade count、trade volume 和 observed value。
2. 在 terminal 处与 exchange cumulative volume 对账，实现缺失部分 last-price 补值算法。
3. 实现纯 side inference，并覆盖 buy、sell、unknown、multi-level、market level、
   locked/crossed、opening/closing、held 和 invalid pre-book。
4. held terminal 发布累计 trade 信息和 raw terminal states，清空 N 档并使 internal book
   invalid；后续完整 book 恢复。
5. format 23 保持一条消息一个 event、64-bit volume 和保守 side contract。

### 阶段 4：迁移 CSV、stats 与 CLI 日志

1. 用 Quill compile-time schema 替换 23 列 header/format，一次性删除 legacy 字段映射。
2. 更新 `ConvertStats` 与完成日志，至少报告：published rows、actual trade payloads、
   match groups、multi-trade groups、held-ended groups、buy/sell/unknown groups、
   incomplete groups 和 missing-volume imputations。
3. 保留 orderbook/basic-info 两文件 staged flush/rename/rollback；失败时不发布 header-only
   或半文件。
4. runner 继续使用同一路径与 `--overwrite`；converter 完成日志记录
   `schema=twse-orderbook-v2`、mode 和 `local_time_source=exchange_fallback`；运行交接摘要
   记录实际使用的 converter binary SHA-256。

### 阶段 5：文档与 migration

1. 重写 `twse_orderbook.md` 为原子 group Orderbook 的唯一事实源，删除独立 Trade 与
   trade-only 单独发布的旧描述。
2. 更新 `twse.md` 的当前/新 schema 边界、Orion 差异、CSV 列数和 value 算法。
3. 更新 converter onboarding 与 testing；明确现有 `/tw_backup` CSV 在重建前仍是 legacy，
   不能与 v2 混读。
4. 不迁移或保留旧 CSV reader；consumer 必须按 exact header 切换。

### 阶段 6：验证与历史重建

1. 运行 focused TWSE tests、完整 CTest、ASan/UBSan focused tests 和 `git diff --check`。
2. 使用 `/tw_backup/data/tw/raw/stock/twse_stock_20260707.dump` 先做 `all` dry-run 和
   `stock` 完整 CSV 到 `/home/liuxiang/tmp`，不覆盖正式数据。
3. 对全日输出运行独立一致性扫描；通过后再用现有 runner 原子重建
   `/tw_backup/data/tw/csv/stock/`，raw dump 与 archive 均保留。
4. 每日问题写入 day log，runner 不因单 symbol issue 停止；全部日期完成后汇总 summary，
   不把大 CSV、dump 或临时扫描结果纳入 git。

## 必须覆盖的测试

- POD：`char[16]`、C arrays、trivial、standard-layout、trivially-copyable、自然对齐；当前
  target 的 `sizeof(Orderbook<5>)` 记录为 272。
- identity：相同 symbol 的 TWSE/TPEx state 隔离，dense `symbol_id` 内部有效且 CSV 不输出。
- state：三个 byte 原样保存、accessor mask 正确、CSV 输出 0..255 数值而非字符。
- book-only：立即发布，trade 增量为 0，actual state 不回退。
- single trade：count=1、volume/side/last/OHLC/value 与 final book 正确。
- multi trade：中间无 callback/CSV；terminal 只发布一条，count/volume/value 汇总全部分片。
- price path：多档不同成交价全部进入 OHLC/value，不能只用 terminal last price。
- interleave：其他 symbol 的 book/trade 可以在 group 中间发布，不拆断原 symbol group。
- held：同时间 held 结束 group、held payload 不算 Trade、发布 aggregate trade、盘口全 0、
  后续完整 book 才恢复。
- trial/open/close/call auction：不污染 actual；方向按 contract 为 unknown。
- side：明确 buy、sell、ambiguous/invalid/market-level/gap unknown。
- total value：exact observed、missing-volume fallback、regular multiplier、odd-lot multiplier 1。
- format：6、17 fixture 使用 group 语义；23 不使用 reserved bit 0 分组且保持大于
  `INT32_MAX` 的 volume。
- recovery：frame corruption、sequence gap、incomplete EOF、missing multiplier、cycle
  mismatch 继续保持成对发布与 `published_partial`。
- CSV：37 列、固定顺序、4 位小数、全部 level volume、terminal sequence、large odd-lot
  int64、Quill flush/close 与 overwrite rollback。

## 真实数据验收

2026-07-07 全 dump 已有独立只读分析基线：

```text
file_bytes=3153093917
messages=25993761
format6_messages=24839014
actual_trade_payloads=2312789
multi_trade_groups=45270
trades_in_multi_groups=103445
held_ended_groups=78
groups_with_different_trade_timestamp=0
incomplete_groups=0
depth_sequence_gaps=0
```

这些数字覆盖 format 6 的 TWSE 与 TPEx 全市场，不等于 `stock` filter 的最终 CSV 行数。
验收时要求：

- `all` dry-run 的 grouping counters 与独立基线一致；若实现 filter contract 导致统计口径
  不同，日志同时给出 source 总数与 filtered 数，不能静默改变定义。
- 每个发布 event 的 `trade_count/trade_volume` 与 raw group 聚合一致。
- 所有 multi group 只发布一行；held group 发布一行且 N 档全 0。
- `trade_count>0` 的 event 使用 terminal sequence；group 内所有 raw trade time 等于该行
  `exchange_ns`。
- 每个 `(market, symbol)` 的 source sequence 增长，actual cumulative volume/value 不回退；
  gap/imputation 必须能在日志定位。
- CSV header 与每行均为 37 列；state/enum 数字合法；价格 4 位小数；大 volume 不截断。
- basic-info 仍为 30 列，主键/排序/去重和既有真实日基线不因本任务改变。
- 记录 wall time、messages/s、rows/s、CSV bytes 与压缩后 bytes，只报告实测，不预设新 schema
  更快或更省空间。

## Review 与提交门

每个阶段完成后做一次 focused diff review，重点检查：

- 是否把 wire fragment 错当成可独立消费事件；
- 是否在 terminal 前泄漏 final book 或 trade side；
- held 后是否错误沿用 stale book；
- per-symbol group 是否被其他 symbol 穿插错误关闭；
- market/source sequence/time/volume unit 是否混用；
- missing/gap/trial 是否污染 actual OHLC/value；
- CSV 字段与文档是否完全一致。

建议原子提交拆分：

1. tests/fixture 与 POD/schema contract；
2. builder/grouping/side/value 实现；
3. CSV/stats/CLI migration；
4. 文档 contract 与真实数据验证结果；
5. 历史 rebuild 只写外部 summary/log，不把数据产物提交 git。

提交前至少运行：

```bash
VCPKG_ROOT=/home/liuxiang/vcpkg \
  cmake --build --preset debug --target twse_converter_tests
ctest --test-dir build/debug --output-on-failure -R 'Twse|twse'
ctest --test-dir build/debug --output-on-failure
git diff --check
```

若 sanitizer preset 不存在，使用项目现有编译选项建立独立 sanitizer build 目录，不修改
默认 preset。

## 回滚

- 代码按上述原子提交逐项 revert；不保留运行时 legacy 开关。
- 正式 v2 CSV 只在单日真实验证通过后使用 staged `--overwrite` 发布。
- raw dump 与压缩包始终保留；需要恢复 v1 时，从迁移前 commit 构建旧 converter 并重新
  生成，不能把 23 列与 37 列文件混在同一 generation。
- 若 grouping 或 side 发现 contract 问题，停止后续日期 rebuild，保留已完成日期与日志，
  先修复并从 raw 重建受影响日期。

## 完成条件

- `Orderbook<5>`、37 列 CSV、状态机、日志和专题文档描述同一 contract。
- format 6/17 不再单独发布 trade-only；normal final 与 held terminal 都只发布一条原子
  match-group Orderbook。
- 单笔/多笔/interleaved/held/trial/gap/odd-lot/value/side fixture 全部通过。
- focused、完整 CTest、sanitizer 和 2026-07-07 全日真实数据验收通过。
- `/tw_backup` 重建只有在验证通过后启动，所有日期结束后有可审计 summary；仓库不包含
  raw、CSV、模型或临时分析产物。
