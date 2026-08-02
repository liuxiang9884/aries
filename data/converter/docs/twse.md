# TWSE / TPEx Dump Converter 设计与 Orion 差异

更新时间：2026-08-01

## 事实源与范围

本实现以以下三项为事实源：

- `data/docs/exchange/TWSE集中市場即時交易資訊傳輸規格書(B.12.11)(202503)_20250113092444.pdf`：
  上市股票，业务别 `01`。
- `data/docs/exchange/上櫃股票IP行情網路規格書(V.12.16 TCPIP).pdf`：上柜股票，
  业务别 `02`。
- `/home/liuxiang/dev/orion` 的 `4282286`：既有行为与历史 CSV 对照。

`twse_dump_converter` 同时处理 TWSE listed 和 TPEx / OTC 消息。它是 offline
dump 到 CSV 的独立工具，不提取 Orion 的 multicast、replay、SHM、symbol
pool 或 binary data file 路径。

## 协议核对结果

两份交易所文档对 format 6、17、22、23 定义相同的 wire layout，市场由
header 的 `service_type` 区分。format 1 的上柜一般股票资料比上市多一个
`stock_type` / 类股注记，因此三个基础价格字段后移 1 byte。

| format | version | 完整消息长度 | converter body span | 用途 |
|---:|---:|---:|---:|---|
| 1 | 9 | 114 | 104 | 一般股票基本资料 |
| 6 | 4 | 32–131 | 22–121 | 一般股票即时行情 |
| 17 | 4 | 32–131 | 22–121 | 权证即时行情 |
| 22 | 1 | 60 | 50 | 盘中零股基本资料 |
| 23 | 1 | 34–155 | 24–145 | 盘中零股即时行情 |

这里的 `body span` 是从 10-byte header 之后开始，包含 1-byte XOR checksum
和 2-byte `0D 0A` terminal code。实现中的关键 zero-based offset 为：

| 字段 | TWSE listed body offset | TPEx / OTC body offset |
|---|---:|---:|
| 今日参考价 / previous close | 30 | 31 |
| 涨停价 | 35 | 36 |
| 跌停价 | 40 | 41 |

## Format1 Basic-info CSV Contract

非 dry-run 转换除 `--output <orderbook.csv>` 外，还必须提供
`--basic-output <basic_info.csv>`。basic-info 不受 orderbook 的
`--symbol-filter-mode` 影响，收集同一 dump 内所有 TWSE / TPEx format1 正常
证券记录。主键和稳定排序键均为 `(trading_day, market, symbol)`；业务别 `01`
映射为 `TWSE`，业务别 `02` 映射为 `TPEX`。

列顺序、类型与单位如下：

| 列 | 内部类型 | CSV 表达 / 单位 | 适用范围 |
|---|---|---|---|
| `trading_day` | `int32` | `YYYYMMDD` | 全部 |
| `market` | string | `TWSE` / `TPEX` | 全部 |
| `symbol` | string | 去除尾端空白的交易所代码 | 全部 |
| `industry_code` | string | format1 原始 ASCII code | 全部 |
| `security_type` | string | format1 原始 ASCII code | 全部 |
| `anomaly_code` | `uint64` | BCD code | 全部 |
| `stock_group_code` | optional string | 原始 code | 仅 TPEx；TWSE 为空 |
| `board_code` | string | 原始 code | 全部 |
| `reference_price` | `double` | 固定 4 位小数 | 全部 |
| `high_limit` | `double` | 固定 4 位小数 | 全部 |
| `low_limit` | `double` | 固定 4 位小数 | 全部 |
| `abnormal_recommendation` | string | 原始 flag | 全部 |
| `special_abnormal` | string | 原始 flag | 全部 |
| `day_trading_code` | string | 原始 code | 全部 |
| `margin_short_exempt` | string | 原始 flag | 全部 |
| `borrow_short_exempt` | string | 原始 flag | 全部 |
| `matching_cycle_seconds` | `uint64` | 秒 | 全部 |
| `warrant_flag` | string | 原始 flag | 全部 |
| `strike_price` | optional `double` | 固定 4 位小数 | 权证；其他为空 |
| `previous_exercise_volume` | optional `uint64` | 实际权证单位 | 权证；其他为空 |
| `previous_cancellation_volume` | optional `uint64` | 实际权证单位 | 权证；其他为空 |
| `outstanding_volume` | optional `uint64` | 实际权证单位 | 权证；其他为空 |
| `exercise_ratio` | optional `double` | 每一权证对应标的数量，固定 5 位小数 | 权证；其他为空 |
| `warrant_upper_price` | optional `double` | 固定 4 位小数 | 权证；其他为空 |
| `warrant_lower_price` | optional `double` | 固定 4 位小数 | 权证；其他为空 |
| `maturity_date` | optional `int32` | `YYYY-MM-DD` | 权证；其他为空 |
| `foreign_stock_flag` | optional string | 原始 flag | 仅 TWSE；TPEx 为空 |
| `multiplier` | `uint64` | 每一标准交易单位包含的证券数量 | 全部；format 6 / 17 的成交额计算使用该值 |
| `currency` | string | ISO-like 3-char code；wire 空白规范化为 `TWD` | 全部 |
| `market_data_line` | `uint64` | BCD `1` / `2` | 全部 |

权证 wire 的三项 volume 以千权证为单位，输出时乘 `1000`；wire
`exercise_ratio` 表示每 1000 权证对应标的数量，输出时除 `1000`，统一为每一
权证口径。所有 price 字段在内部使用 `double`，便于后续研究。Big5/CP950
`name` 和非十元面额 flag 不解析、不输出。

权证判断使用 format1 的 `warrant_flag`、`security_type` 和
`market_data_line`，任一判据成立即按权证字段解析；2026-07-07 完整数据中三个
判据完全一致：37,937 行均同时满足，另有 2,904 行均不满足。orderbook 的既有
filter mode 语义不变。

### 控制记录、重复与冲突

- `stock_entries` 为空才是证券记录；`AL` / `NE` 是循环控制记录，不写 CSV。
- dump 可能从发布循环中段开始。每个 market 的首个 `AL` / `NE` 只用于建立
  周期边界；从下一完整周期起比较控制记录中的 ASCII 数量与该周期实际收到的
  format1 正常记录数。二者不一致时记录 service、`AL` / `NE`、expected、actual、
  missing、offset 与 sequence，转换继续并把当日标为 `published_partial`。
- 同主键且全部输出字段一致的重复记录忽略并计数。
- 同主键任一输出字段变化时转换失败，错误包含旧/新 byte offset、sequence 和
  字段差异。format1 没有足以安全决定 last-wins 的事件时间，禁止静默覆盖，
  以避免在研究资产池中引入未知时点状态或 look-ahead。

format 6 / 17 每档为 5-byte price 加 4-byte volume；format 23 每档为
5-byte price 加 6-byte volume。即时行情顺序均为成交、bid 一至五档、ask
一至五档。`data_flag`、`limit_flag`、`status` 按三个原始 byte 组合成 CSV
的整数 `status`，与 Orion x86 输出一致，但不依赖 C++ bitfield 排列。

## 与 Orion 保持一致的行为

- 只消费 format 1、6、17、22、23，其他合法 frame 解码后忽略。
- 提供 `stock`、`etf`、`warrant`、`odd_lot`、`all` 五种 mode。
- `stock` 沿用 Orion 的 `symbol[4] == ' '` 判断，因此四字符 ETF 也会进入
  `stock` 输出。
- `warrant` mode 使用 stock-or-warrant filter；format 6 只更新状态，format
  17 才写 CSV。
- warrant symbol 除 Orion 代码形态外，也可由先到达的 format1 权证元数据识别。
- `all` mode 对所有 symbol 的 format 6 写 CSV，但不会写 format 17；这是
  Orion 当前 mode 语义，不把 `all` 解释为所有 format。
- 每个 symbol 维护 previous close、涨跌停、open、last 和累计 volume。
- orderbook CSV 为 23 列，不包含 bid / ask volume；price 和 `total_value` 保留
  两位小数。
- offline `symbol_id = -1`，`localtime = exchtime`。

## 有意不同于 Orion 的行为

| 项目 | Orion `4282286` | Aries | 原因与影响 |
|---|---|---|---|
| 输入配置 | TOML；`trading_day` 缺省时取当天 | CLI 直接要求 `--trading-day`、`--dump`，非 dry-run 要求 `--output` 与 `--basic-output` | 让 offline 任务参数显式、可复现 |
| 交易日起点 | Nova `mktime`，依赖进程时区 | 固定 UTC+8 自然日零点 | 避免服务器时区改变 timestamp |
| wire 表达 | packed struct、bitfield、`reinterpret_cast` | byte span、显式 offset 和 mask | 消除 ABI、对齐和 bitfield 顺序依赖 |
| frame 校验 | 按 header length 读取，不核对 XOR、terminal 或最小 trailer | 每条消息校验长度、XOR checksum、`0D 0A` terminal；损坏后只向前扫描最多 1 MiB，并要求候选 frame 完整通过全部校验 | 发布错误前的可信数据，并在可安全重同步时继续未受影响 symbol；不静默接受损坏 frame |
| BCD 校验 | nibble 直接参与计算 | 拒绝非十进制 nibble、非法时间与溢出 | 防止错误数字进入时间和价格 |
| 协议版本 | 不检查 `service_type` / `format_version` | 只接受 service 01/02；已处理 format 只接受文档版本 9/4/1 | 新版 layout 未审查前不静默误解码 |
| 动态档位 | 根据 bitfield 直接遍历 | 先验证最多五档及精确动态 body 长度 | 避免越界或错位读取 |
| 结束控制消息 | `000000` 会被 `warrant` / `all` filter 接受，随后把 `999999999999` 当时间 | 所有 mode 都先忽略协议定义的结束控制消息 | 修复 `warrant` / `all` 完整 dump 末尾失败；有效行情不变 |
| mode 拼写 | 未知字符串退回 `stock` | CLI 和 parser 都拒绝未知 mode | 防止拼写错误生成错误资产池 |
| 输出发布 | writer 直接打开目标文件 | orderbook/basic-info 同时写入同目录 `.partial.<pid>`，完成后成对 rename；局部质量问题允许发布可信子集 | 兼顾研究数据可用性与问题可追溯性；不可恢复的全局错误仍保留旧输出 |
| 异常上下文 | 多数解析路径没有输入 byte offset | 错误包含 message 起始 byte offset | 便于定位损坏 dump |
| format1 输出 | 只更新 orderbook 内部状态 | 另生成 30 列、去重且排序的 basic-info CSV | 保留可研究的证券与权证基础资料 |
| format1 控制 | `AL` / `NE` 可能成为 pseudo symbol | 识别控制记录，并在首轮同步后校验每个完整周期数量；mismatch 记录后继续 | 防止计数记录污染证券状态，检测并显式暴露丢包，同时保留其余可信数据 |
| format1 重复 | 同 symbol 后到状态覆盖 | 相同记录去重；字段变化即失败并报告 diff | format1 无安全的 last-wins 时间语义 |
| `total_value` | `delta_volume * last_price`，未乘交易单位 | 一般交易累计 `delta_volume * last_price * multiplier`；odd-lot 的 wire volume 已是实际证券数量，有效乘数为 1 | 统一为实际币种成交额；orderbook CSV 不再与 Orion byte-compatible |
| CLI 日志 | 工具原有输出方式 | outer CLI 使用 Nova `INFO` / `WARNING` / `ERROR`；core 返回结构化质量统计或通过异常传递全局错误 | 每个 recoverable issue 和最终 publication status 都可由 runner 收集 |

2026-07-07 的旧验证曾证明修改前 orderbook CSV 与 Orion bytes 完全一致；该 hash 只
作为历史基线。自 2026-08-01 起 `total_value` 纳入 format1 `multiplier`，因此新
orderbook CSV 与 Orion 不再 byte-compatible，其他 22 列的既有语义不变。

`total_value` 不是交易所直接提供的字段。一般交易按相邻消息累计量差、当前成交价
和 format1 multiplier 计算，币种来自 basic-info `currency`；若成交量变化前尚未
收到 multiplier metadata，不伪造乘数或成交额，该 symbol 从此不再输出 orderbook，
并在日志中记录 `missing_multiplier`。format 23 odd-lot 的 volume 已经是实际证券
数量，因此不再乘标准交易单位。

## Best-effort 恢复与发布 contract

- 非 dry-run 始终把 orderbook 与 basic-info 作为同一 generation 成对发布；某一类没有
  可输出记录时，对应文件只包含稳定 header。
- frame header、长度、checksum 或 terminal 失败后，从错误 offset 的下一 byte 开始
  向前扫描最多 1 MiB。候选位置必须完整通过 header、长度、checksum 和 terminal
  校验才是重同步点；未找到时停止读取后缀，但仍可发布此前的可信前缀。
- 如果损坏 frame 的 body 能提供可打印的 symbol，converter 将该 symbol 标记为
  invalidated，重同步后不再输出它的 orderbook。这样不会用跨缺口的累计 volume 和未知
  成交价继续计算错误的 `total_value`；其他 symbol 继续转换。
- format1 cycle mismatch、局部 frame recovery 和 missing multiplier 都是可恢复质量
  问题，最终状态为 `published_partial`。没有上述问题时为 `published_complete`。
- 如果非空 dump 中连一条通过 frame 校验并成功处理的消息都没有，不发布只有 header
  的新文件。format1 同主键字段冲突等无法安全决定状态的语义错误仍使转换失败，并
  保留既有成对输出。
- `data/converter/scripts/rebuild_twse_csv` 通过 converter exit status、最终日志状态
  以及两份输出 inode 是否在本轮同时变化判定发布结果，区分
  `published_complete`、`published_partial`、`preserved_previous` 和
  `not_published`，不会再把预先存在的 CSV 当成本轮成功。

## 当前验证边界

- 上市和上柜 format 1 offset 分别有 fixture；TPEx format 1 + format 6 已有
  端到端 CSV 测试。
- format 6 / 17 / 22 / 23、五种 mode、checksum、trailer、截断、超过五档、
  有界重同步、受影响 symbol 隔离、缺失 multiplier、双文件 best-effort 发布、
  原子发布和 symlink 边界均有 focused tests。
- 2026-07-07 stock dump 含 listed 与 OTC stream，严格 checksum / version /
  service 校验下可完整读取 25,993,761 条消息并生成 15,886,026 条数据行。
- 同一 dump 含 1,145,472 条 format1 正常记录与 167 条控制记录；去除
  1,104,631 条完全相同的重复后得到 40,841 个唯一主键，未发现字段变化冲突。
- basic-info CSV 为 5,118,361 bytes、40,841 个数据行、30 列，SHA-256 为
  `093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a`；
  其中 TWSE 30,488 行、TPEx 10,353 行。
- 该 dump 没有可供 `odd_lot` format23 / `warrant` format17 orderbook 输出的真实
  消息；这两个 orderbook 输出路径目前仍由 synthetic fixture 覆盖。
- 当前 orderbook CSV 丢弃五档 volume。包含 volume 的正式研究 schema 需要单独设计、
  版本化和迁移，不能直接修改现有 23 列输出。

## 输出实现边界

- orderbook 与 basic-info 都使用
  `quill::CsvWriter<Schema, nova::LogManager::NovaFrontendOptions>`；header 与 format
  由 compile-time schema 固定，typed field 通过 `append_row()` 进入 Nova 管理的
  Quill backend。本次迁移保持当前 23/30 列、字段语义和数值格式不变。
- writer 只打开同目录 `.partial.<pid>`；发布前依次执行 blocking `flush()`、销毁
  Quill writer 并确认 partial 是非空 regular file，确保 backend 不再持有输出文件后
  才进入 rename。
- 两个输出均先写同目录 partial，完成后才进入发布；`--overwrite` 会先把两份旧
  输出移到 backup，任一 rename 在进程内失败时回滚新文件并恢复旧文件。
- Quill backend 的异步格式化/磁盘错误不能从 `append_row()` 同步返回；Nova/Quill
  日志是这类错误的直接诊断入口，partial 检查只能拦截文件缺失、类型错误和零字节结果，
  不等价于逐行同步 I/O 错误传播。
- 两次 final rename 不是文件系统级事务。进程被强杀、主机崩溃或断电发生在两次
  rename 之间时，无法保证跨文件原子性；下游应在任务成功退出后再消费两个文件，
  后续 manifest 设计应补充跨文件 commit marker / generation id。
- format22 仍只服务 odd-lot orderbook 的内部基础状态，本轮没有并入 basic-info CSV。
