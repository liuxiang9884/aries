# TAIFEX dump converter 实现计划

> 本文保留初版实施计划与当时的 45 列/`continuous_flag` 设计记录。2026-08-01
> 后续统一 contract 已改为 44 列、`total_value = abs(price) * volume * multiplier`，
> gap/metadata 质量改写入逐日日志；当前事实以 `../taifex.md` 和
> `2026-08-01-unified-market-value.md` 为准。

## 目标

在 `data/converter/taifex/` 实现不依赖 shared memory 的同步 converter，直接把
TAIFEX futures multicast dump 转换为研究可用的 depth CSV 与 basic-info CSV。
实现以交易所《期货逐笔行情资讯传输作业手册 V1.5.1》为协议事实源，以 Orion
的 data engine、data reader 和 order book builder 为行为参考，但不复制其
SHM、异步 reader 或在线运行结构。

完成标准：

- 严格校验 frame header、BCD、body length、checksum 和 `CRLF`；遇到损坏时
  输出包含 byte offset 与消息上下文的错误，并且不发布 partial CSV。
- 解析 futures basic message I010/I011，以及 realtime I024/I025/I081/I083/
  I084；处理 I001 heartbeat 与 I002 reset，其他已知 futures 消息只做 frame
  校验和计数。
- 同步构造每个 futures product 的五档普通与衍生 order book，不依赖 SHM、
  multicast、TOML、Quill 或异步队列。
- 支持 outright 与 calendar spread；不把 stock-future filter 固化到 converter。
- 在 `/data/tw/raw/future/` 的真实 dump 上完成 dry-run、生成 CSV，并校验 schema、
  行数、排序/时间、数值和代表性 symbol。

## 非目标

- 本轮不实现 options transmission code `4/5/6`。
- 不解析 Big5/CP950 商品名称。
- 不复用 live engine，也不增加实盘路径中的严格校验。
- 不推断 PDF V1.5.1 未定义的 I010 version 9 扩展 8 bytes。
- 不把原始 dump、解压文件、生成 CSV 或大体积验证产物提交到 git。

## 数据流与目录

```text
taifex_dump_converter
  -> sequential dump reader
  -> strict frame decoder
  -> I010 product catalog + I011 contract catalog
  -> per-symbol synchronous order book builder
  -> staged depth/basic-info CSV files
  -> atomic publish after exact EOF
```

实现和头文件都放在 `data/converter/taifex/`；测试放在
`tests/data/converter/taifex/`。模块设计、schema、Orion 差异与真实数据验证写入
`data/converter/docs/taifex.md`，当前接手状态写入模块 onboarding。

## Wire contract

公共 header 固定 19 bytes：`ESC`、transmission code、message kind、BCD time、
channel id、channel sequence、version 和 body length。body 后固定为 checksum 1
byte 与 `CRLF` 2 bytes。checksum 是从 header 第二个 byte 到 body 最后一个 byte
的 XOR，不含 `ESC`、checksum 与 `CRLF`。

本轮严格处理的 futures 消息：

| 消息 | transmission/kind | version | body contract |
|---|---|---:|---|
| I001 heartbeat | `0/1` | 1 | 0 bytes |
| I002 reset | `0/2` | 依文档/实样 | reset product state |
| I010 product basic | `1/1` | 9（真实数据） | 40 bytes；解析文档定义的前 32 bytes |
| I011 contract basic | `1/3` | 4 | 65 bytes |
| I081 incremental book | `2/A` | 1 | `26 + count * 13` |
| I083 full book | `2/B` | 1 | `27 + count * 12` |
| I084 recovery snapshot | `2/C` | 3 | A/O/S/P/Z 可变长结构 |
| I024 trade | `2/D` | 1 | `55 + count * 8` |
| I025 intraday high/low | `2/E` | 1 | 43 bytes |

对 handled message 的未知 version、错误 exact length、无效 enum/level/sign/BCD
直接失败。其他 transmission/kind 在 frame 完整校验后计入 ignored，不按未知 body
布局解码。

## Metadata 与 symbol 解析

- I010 提供 symbol 级 reference price、decimal locator、上市/下市/交割日等。
- I011 提供 kind 级 multiplier、currency、decimal locator、underlying type 等。
  `contract_size` 按 `9(7)V9(4)` 解码，例如 `000020000000 / 1e4 = 2000`。
- outright 优先使用 I010 的 decimal locator，并校验它与对应 I011 kind 一致；
  calendar spread 没有 I010 时，使用前三字符 kind 对应的 I011 decimal locator 和
  multiplier。
- 真实 20260707 样本中的 I011 kind 都是三字符；若未来出现不能唯一匹配的 kind，
  converter 失败而不是猜测。
- realtime 早于 metadata 的消息不能安全解释价格。该消息按 symbol 计数并跳过；
  metadata 到达后的 product sequence gap 交给 I084 恢复。不会默认为 decimal 0。
- I010/I011 重复记录允许完全一致的重复；影响输出的字段冲突时失败。

## Depth CSV contract

一行代表 I081 或 I083 应用后的完整状态；I024/I025 只更新状态，在下一条 book
消息中体现。列固定为：

```text
trading_day,market,symbol,symbol_id,exchtime,localtime,
reference_price,open,high,low,last_price,trade_volume,total_volume,total_value,
total_buy_count,total_sell_count,
ask_price1,ask_volume1,bid_price1,bid_volume1,...,
ask_price5,ask_volume5,bid_price5,bid_volume5,
derived_ask_price,derived_ask_volume,derived_bid_price,derived_bid_volume,
match_flag,build_type,orderbook_action,continuous_flag,sequence
```

语义：

- `market=TAIFEX`，`symbol_id=-1`，offline `localtime=exchtime`。
- `exchtime` 是以 CLI `trading_day` 的 Asia/Taipei 当地零点为基准，加 header
  event time 后得到的 Unix epoch nanoseconds。本轮真实数据为日盘；夜盘跨交易日
  日期映射尚不在已验证范围内。
- 所有价格和 `total_value` 在 C++ 中使用 `double`；CSV 固定输出 6 位小数。
- `trade_volume` 是上一次输出之后实际成交的 contract 数；输出后归零。
- `total_volume` 采用 I024 exchange summary 的累计 contract 数。
- `total_value` 累计 `price * contracts * multiplier`；发生无法完整恢复的 sequence
  gap 后它只能代表 converter 看到的成交额，必须结合 `continuous_flag` 使用。
- `total_buy_count`/`total_sell_count` 是 I024 的买/卖方累计交易笔数，不再写到名为
  volume 的字段。
- `match_flag` 输出语义值 `0`（actual）或 `1`（trial），不是 ASCII code 48/49。
- `build_type=0` 表示 I081，`3` 表示 I083；`orderbook_action=1` 表示该 I081
  含 insert/delete，其他为 0。
- `continuous_flag=0` 表示当前行经过 snapshot recovery 或已知缺口影响；正常连续
  处理为 1。

## Basic-info CSV contract

每个 I010 product 输出一行；realtime 出现但没有 I010 的 spread 也输出一行，
product 级字段留空并用 `basic_source=I011` 标记。列固定为：

```text
trading_day,market,symbol,kind_id,is_spread,basic_source,contract_type,
contract_subtype,reference_price,decimal_locator,strike_decimal_locator,
listing_date,delisting_date,delivery_date,flow_group,dynamic_banding,
multiplier,currency_code,currency,stock_id,contract_status,quote_flag,
block_trade_flag,expiry_type,underlying_type,close_group,end_session
```

不输出 Big5 name 和 I010 未知扩展 bytes。日期输出 `YYYY-MM-DD`，缺失字段为空。

## Order book 与 sequence

- I081 entries 必须按消息内顺序应用。普通 bid/ask 的 new/delete 会移动后续档位，
  change 原位替换；derived bid/ask 只接受 overlay。
- I083 是完整快照，应用前清空普通五档和 derived quote，再逐项写入。
- I024/I025/I081/I083 共用 product sequence。连续消息直接应用；duplicate/old
  message 丢弃并计数；gap 后缓存按值解码的消息，等待 I084 O snapshot。
- I084 O 用 `last product sequence` 重建 book，然后只在缓存从 snapshot 后一条开始
  严格连续时 replay。I084 S 按字段合并可恢复的成交、高低、累计量和笔数，不覆盖
  snapshot 之后已经 replay 的更新。发生已知缺口后，同一 session 的后续行持续标记
  `continuous_flag=0`，直到 I002 reset，避免把无法恢复的累计 value 当作完整 history。
- I002 清空所有 realtime book、sequence 和 gap cache，保留当日 basic catalog。
- 每个 symbol 的 gap cache 有硬上限；超过上限失败，防止坏数据无限占用内存。

## 与 Orion 的有意差异

- 同步直接处理，不创建 SHM、不经 data reader、不使用 Quill CsvWriter；CSV 用
  `std::ofstream` + `fmt` staged writer，Nova 只记录 CLI 日志。
- 默认转换全部 futures，而不是只保留 near-two-month stock futures。
- I083 按交易所定义清空整个普通与 derived book；Orion 仅清空缺失普通档。
- I024 的 packet trade volume 按各 entry volume 正确求和，不重复累加累计值；
  `total_volume` 直接采用 exchange summary。
- 正确命名 buy/sell trade count，不冒充 bid/ask volume；不输出始终未维护的
  `total_trade`。
- 第一笔 actual trade 正确初始化 open/high/low；I025 不再因先覆盖 exchtime 而被
  无效比较跳过。
- 使用显式开盘状态而不是 `open == 0` sentinel，支持零价 calendar spread 开盘。
- gap cache 持有 decoded value，不持有 SHM buffer pointer；snapshot recovery 在检查
  cache 非空和边界后执行。
- frame 检查失败即中止并保留旧输出；live 路径不受影响。

## 实施与验证顺序

1. 用小样本 builder 建立 BCD/header/checksum、I010/I011、I024/I025、I081/I083/
   I084 与 atomic publication 的失败测试。
2. 实现 wire decoder、metadata catalog、order book、CSV writer、dump reader 和 CLI。
3. Debug build 跑 focused tests；修复后做 source diff review 与协议字段逐项 review。
4. Release build 跑全部测试。
5. 从 `taifex_20260707.dump.tar.gz` 解压到 `/home/liuxiang/tmp`，先 dry-run，再生成
   CSV；验证 exact EOF、消息统计、depth/basic 行数、CSV 列数、时间单调边界、
   signed price、volume 非负约束、bid/ask 档位顺序、multiplier 覆盖和
   representative symbols。
6. 删除临时解压 dump，只保留压缩包和用户需要的最终 CSV；记录命令、统计、文件
   checksum、已知边界与复现入口。
7. 更新模块文档/onboarding，运行 `git diff --check`、测试与最终 review，按最小闭环
   原子提交；不主动 push。

## 风险与回滚

- PDF 为 V1.5.1，但真实 I010 已到 version 9。只解析文档已定义的前 32 bytes，要求
  body 精确为真实 40 bytes，并把其余 8 bytes 明确记录为未解释字段。
- 夜盘日期映射、没有 I084 可恢复快照的真实 packet loss、以及 multiplier 变更造成的
  历史累计 value 不可逆恢复，是本轮需要显式报告的边界。
- CSV 只在成功读到 exact EOF 后从 `.partial.<pid>` 原子发布；失败时旧文件不变。
  回滚代码只需删除 TAIFEX target/files，不改变 TWSE converter contract。

## 实施结果

计划已于 2026-08-01 完成。同步 converter、focused tests、strict EOF/gap
finalization、模块文档和 CLI 均已实现；20260707 完整 dump 已 dry-run 并生成正式
depth/basic-info CSV。最终消息统计、输出 hash、独立全表检查和已知边界以
`data/converter/docs/taifex.md` 为事实源。
