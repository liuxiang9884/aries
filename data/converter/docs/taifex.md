# TAIFEX Futures Dump Converter 设计与验证

更新时间：2026-08-01

## 范围与事实源

`taifex_dump_converter` 把 TAIFEX futures multicast dump 同步转换为五档 depth
CSV 和 basic-info CSV。它覆盖所有 futures outright 与 calendar spread，不内置
stock-future、近月或交易时段 filter；资产池筛选交给下游研究流程。

协议事实源为：

- `data/docs/exchange/期貨逐筆行情資訊傳輸作業手冊(V1.5.1).pdf`。
- `/home/liuxiang/dev/orion` 的 TAIFEX data engine、data reader、order book
  builder 与 dump CSV builder，作为既有行为和缺陷对照。
- `/data/tw/raw/future/taifex_20260707.dump.tar.gz` 的真实 wire version 与字段
  取值。

实现位于 `data/converter/taifex/`，不依赖 SHM、multicast、TOML、public symbol
pool 或异步 reader。core 使用异常返回错误；Nova 只用于 CLI 的完成/失败日志。

## 使用方式

先解压归档，再运行 converter：

```bash
./build/release/data/converter/taifex_dump_converter \
  --dump /path/to/taifex_20260707.dump \
  --trading-day 20260707 \
  --output /data/tw/csv/future/taifex_20260707_future.csv \
  --basic-output /data/tw/csv/future/taifex_20260707_future_basic_info.csv
```

只做完整协议和状态校验：

```bash
./build/release/data/converter/taifex_dump_converter \
  --dump /path/to/taifex_20260707.dump \
  --trading-day 20260707 \
  --dry-run
```

默认拒绝覆盖已有输出；`--overwrite` 只在明确需要原子替换时使用。非 dry-run
必须提供两个不同的输出路径。

## 同步数据流

```text
sequential dump reader
  -> strict frame validation
  -> I010 product catalog + I011 contract catalog
  -> per-symbol product sequence + order book state
  -> synchronous staged depth/basic CSV writer
  -> exact EOF 后发布
```

每条 frame 固定为 19-byte header、`body_length` bytes body、1-byte checksum 和
`0D 0A`。checksum 是从 header 第二个 byte 到 body 最后一个 byte 的 XOR。
converter 对 `ESC`、所有 BCD digit/time、body exact length、handled version、
checksum 和 terminal code 做校验；错误包含 dump byte offset、transmission、kind、
version、channel 和 channel sequence。

## 已处理消息

| 消息 | transmission/kind | version | 行为 |
|---|---|---:|---|
| I001 heartbeat | `0/1` | 1 | 校验空 body |
| I002 reset | `0/2` | 1 | 清空 realtime book、sequence 与 cache，保留 metadata |
| I010 product basic | `1/1` | 9 | 解析已知前 32 bytes；真实扩展 8 bytes 不解释 |
| I011 contract basic | `1/3` | 4 | 解析 multiplier、currency、decimal locator 等 |
| I081 incremental book | `2/A` | 1 | 按 entry 顺序更新，输出完整状态 |
| I083 full book | `2/B` | 1 | 先清空普通五档与 derived quote，再输出完整状态 |
| I084 recovery snapshot | `2/C` | 3 | 解析 A/O/S/P/Z；仅在 product sequence gap 时接纳 O/S |
| I024 trade | `2/D` | 1 | 更新成交、OHLC、累计 volume/value/count，不直接输出 |
| I025 intraday high/low | `2/E` | 1 | 更新 high/low，不直接输出 |

同一 product 的 I024/I025/I081/I083 共用 `PROD-MSG-SEQ`。其他合法 futures
消息完成 frame 校验后只计入 `ignored_messages`，不猜测未实现 body。

## Metadata 与 multiplier

I010 是 symbol 级资料，提供 reference price、decimal locator、上市/下市/交割日、
flow group 等。I011 是三字符 `kind_id` 级资料，`CONTRACT-SIZE` 按
`9(7)V9(4)` 解码成 `double multiplier`，例如 raw `000020000000` 输出
`2000.0000`。

outright 同时使用 I010 和对应 I011，并要求 decimal locator 一致。calendar
spread 没有 I010，使用 symbol 前三字符匹配 I011，因而仍可正确解码 signed price
和 multiplier。若 realtime 早于 metadata，禁止默认为 decimal 0：消息计入
`metadata_missing_messages` 并跳过，后续 sequence gap 由 I084 恢复。

I010/I011 会在盘中轮播。影响输出的字段完全相同则去重并计数；同 symbol/kind 的
字段发生冲突时转换失败。I011 的 Big5/CP950 `NAME` 不解析、不输出。

## Depth CSV contract

depth CSV 固定为 45 列：

```text
trading_day,market,symbol,symbol_id,exchtime,localtime,
reference_price,open,high,low,last_price,trade_volume,total_volume,total_value,
total_buy_count,total_sell_count,
ask_price1,ask_volume1,bid_price1,bid_volume1,...,
ask_price5,ask_volume5,bid_price5,bid_volume5,
derived_ask_price,derived_ask_volume,derived_bid_price,derived_bid_volume,
match_flag,build_type,orderbook_action,continuous_flag,sequence
```

关键语义：

- `market=TAIFEX`，offline `symbol_id=-1`，`localtime=exchtime`。
- `exchtime` 是 CLI `trading_day` 的 UTC+8 当地零点加 header event time，单位为
  Unix epoch nanoseconds。
- 所有 price 和 `total_value` 的内部类型为 `double`，CSV 固定 6 位小数。
- `trade_volume` 是上次 depth 输出之后的成交 contract 数，输出后归零；
  `total_volume` 直接采用 I024 `MATCH-TOTAL-QTY`。
- `total_value` 累计 `signed price * contracts * multiplier`。因此 calendar
  spread 可以为负；这是 signed spread notional，不等同于保证金、现金成交额或
  underlying notional。
- `total_buy_count`、`total_sell_count` 是 exchange 累计成交笔数，不是盘口量。
- `match_flag` 输出 `0=actual`、`1=trial`，不输出 ASCII 数值 48/49。
- `build_type=0` 表示 I081，`3` 表示 I083；`orderbook_action=1` 表示本条 I081
  包含 insert/delete。
- `continuous_flag=0` 表示 converter 已知该 symbol 的历史发生缺口。I084 可以
  恢复当前 book、total volume 和统计值，但不能恢复缺失成交的累计
  `total_value`，因此该标志在同一 session 后续行保持 0，直到 I002 reset。

一行只由 I081/I083 触发。I024/I025 的更新合并到下一条 book 行；这是 event
state CSV，不是逐类消息明细表。

## Basic-info CSV contract

basic-info CSV 固定为 27 列：

```text
trading_day,market,symbol,kind_id,is_spread,basic_source,contract_type,
contract_subtype,reference_price,decimal_locator,strike_decimal_locator,
listing_date,delisting_date,delivery_date,flow_group,dynamic_banding,
multiplier,currency_code,currency,stock_id,contract_status,quote_flag,
block_trade_flag,expiry_type,underlying_type,close_group,end_session
```

记录按 `symbol` 排序且唯一：

- I010 outright 的 `basic_source=I010+I011`，product 与 contract 字段都有值。
- realtime/snapshot 中出现而没有 I010 的 spread，`basic_source=I011`，product
  专属字段为空。
- `is_spread` 根据 symbol 是否包含 `/` 输出 0/1。
- price 固定 6 位、multiplier 固定 4 位、日期使用 `YYYY-MM-DD`、非适用字段为空。
- currency code `1..8` 映射为 `TWD/USD/EUR/JPY/GBP/AUD/HKD/CNY`。

## Sequence、snapshot 与错误策略

普通消息必须严格接续当前 product sequence。重复/旧消息不重复应用；发现 gap
后，decoded event 按值缓存，不保留 dump buffer pointer。I083 自身是完整 book，
可以恢复 book sequence；I084 O 只在等待恢复时接纳，再 replay snapshot 之后严格
连续的 cache。配套 I084 S 只更新同一轮已接受 O 的 symbol，并按最近已 replay 的
trade/high-low sequence 分字段合并：可安全恢复的统计会接纳，较新的实时状态不会被
旧 snapshot 回滚；正常轮播也不会覆盖实时状态。

每个 symbol 的 cache 上限为 8,192 条。exact EOF 时仍有未恢复 gap 会使转换失败，
不发布 CSV。frame/header/body/basic conflict、未知 handled version、非法 enum/level、
cache overflow 和输出 I/O 错误同样失败。

两份输出先写同目录 `.partial.<pid>`。完整解码、EOF finalization、basic-info 排序和
成功 close 后才发布；`--overwrite` 会先备份两份旧文件，并在进程内 rename 失败时
回滚。两次 final rename 仍不构成断电级跨文件事务，下游只能在进程成功退出后消费。

## 与 Orion 的有意差异

| 项目 | Orion | Aries |
|---|---|---|
| offline 架构 | DataEngine 写 SHM，DataReader 再构 book/CSV | 单进程同步 decode/build/write，无 SHM |
| 资产池 | to_csv 只留近两月标准 stock futures | 全部 futures outright 与 spread |
| CSV writer | Quill async CsvWriter | `std::ofstream` + `fmt` 同步 staged writer |
| I083 | 只清空缺失普通档，derived 可能残留 | 按文档先清空整个普通与 derived book |
| I024 packet volume | extra trade 路径重复累加累计 packet volume | 每个 trade quantity 只累计一次，exchange summary 作为 total volume |
| OHLC | 第一笔成交以旧 last price 初始化 high/low | 第一笔 actual trade 直接初始化 open/high/low |
| 零价开盘 | 以 `open == 0` 兼作未初始化 sentinel | 独立记录开盘状态，零价 spread 不会被后续成交改写 open |
| I025 | 先覆盖 exchtime 后比较，high/low 更新可能失效 | 解码后按 product sequence 更新 high/low |
| 买卖统计 | 写入名为 bid/ask volume 的字段 | 正确命名为 buy/sell trade count |
| multiplier/value | value 未乘 contract multiplier | I011 解析 multiplier，value 按 contract size 计算 |
| flag | `match_flag` 可能输出 ASCII 48/49 | 输出语义值 0/1 |
| gap cache | 保存 SHM buffer pointer，部分 empty/end 边界不安全 | 保存 decoded value，边界检查并在 EOF 拒绝未恢复 gap |
| I084 S | 未合并 recovery statistics | 按字段 sequence 合并，恢复缺失统计且不回滚较新事件 |
| frame 检查 | dump 路径不完整校验 checksum/terminal | 每条 frame 严格校验并原子保护旧输出 |

Aries 不是 Orion 40 列 stock-future CSV 的 byte-compatible 替代品；这是包含
volume、multiplier、全部 futures 和明确恢复语义的新 research schema。

## 已知边界

- V1.5.1 文档定义的 I010 body 为 32 bytes，20260707 真实 feed 为 version 9、
  40 bytes。当前只使用文档可确认的前 32 bytes，并要求真实 body 精确 40 bytes；
  未知扩展 8 bytes 不进入研究字段。
- 当前 timestamp contract 已由 20260707 日盘验证。夜盘属于下一交易日时，尤其
  周一对应前一周五夜盘，不能仅凭 `HHMMSS` 与交易日安全推断事件自然日；在引入
  交易日历映射前，夜盘日期语义属于未验证边界。
- gap 后 I084 可恢复 book 与 exchange summary，不能重建遗漏成交的精确累计 value；
  必须使用 `continuous_flag` 过滤或分层分析。
- I140、I012、I030、I070-I073 等合法消息当前只做 frame 校验，不输出其状态字段。

## 2026-07-07 真实数据验证

输入归档为 `/data/tw/raw/future/taifex_20260707.dump.tar.gz`，压缩文件
1,213,053,544 bytes，内部唯一 member `taifex_20260707.dump` 为
6,005,844,926 bytes。Release converter 对完整 dump 的结果：

```text
messages=62559197 depth_rows=54221272 symbols=4839
i010=181201 i011=66096 basic_duplicates=245178 basic_rows=4839
ignored=2496774 metadata_missing=9 sequence_gaps=4 stale=0 recoveries=4
resets=0 bytes=6005844926
```

正式输出：

| 文件 | bytes | 数据行 | 列数 | SHA-256 |
|---|---:|---:|---:|---|
| `/data/tw/csv/future/taifex_20260707_future.csv` | 18,034,209,936 | 54,221,272 | 45 | `5dd579b47fd1b0ee803c076d36cef58e14631741c9672051283ca16a228688f1` |
| `/data/tw/csv/future/taifex_20260707_future_basic_info.csv` | 487,542 | 4,839 | 27 | `200f3c86d6f788a99f23d733f4ccdd28c91044085963c4d4c32b9ea0b6094577` |

独立于 converter 的完整 CSV 扫描确认：

- 54,221,272 行全部为 45 列，2,923 个实际产生 depth 行的 symbol 内 sequence
  严格递增。
- `trading_day/market/symbol_id`、`localtime=exchtime`、volume 非负、flag 枚举和
  high/low 约束无异常；时间范围为 `1783382563804000000` 至
  `1783406701020000000`。
- 43,388 行存在负的 fixed-point price/value，涉及 864 个 symbol，全部为
  calendar spread；没有 negative-price outright。
- 4 个发生过已恢复 gap 的 symbol 为 `TJFG6`、`TJFH6`、`TJFH6/L6`、`TJFL6`，
  共有 441,094 行保守标记 `continuous_flag=0`。
- basic-info symbol 排序且唯一，1,760 行来自 `I010+I011` outright，3,079 行
  来自 I011 spread；所有 multiplier 大于 0，decimal locator 与 product/contract
  metadata 一致。
- basic contract type 分布为 S 4,626、I 164、E 29、C 20；currency 分布为
  TWD 4,803、USD 19、CNY 12、JPY 5。
- 输出目录没有 `.partial.*` 或 `.backup.*` 残留。
