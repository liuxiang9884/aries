# TWSE / TPEx Dump Converter 设计与 Orion 差异

更新时间：2026-07-31

## 事实源与范围

本实现以以下三项为事实源：

- `data/docs/exchange/TWSE集中市場即時交易資訊傳輸規格書(B.12.11)(202503)_20250113092444.pdf`：
  上市股票，业务别 `01`。
- `data/docs/exchange/上櫃股票IP行情網路規格書(V.12.16 TCPIP).pdf`：上柜股票，
  业务别 `02`。
- `/home/liuxiang/dev/orion` 的 `4282286`：legacy CSV 行为基准。

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

非 dry-run 转换除 `--output <depth.csv>` 外，还必须提供
`--basic-output <basic_info.csv>`。basic-info 不受 depth 的
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
| `multiplier` | `uint64` | format1 数值原值 | 全部；与 TAIFEX 的统一语义待后续决定 |
| `currency` | string | ISO-like 3-char code；wire 空白规范化为 `TWD` | 全部 |
| `market_data_line` | `uint64` | BCD `1` / `2` | 全部 |

权证 wire 的三项 volume 以千权证为单位，输出时乘 `1000`；wire
`exercise_ratio` 表示每 1000 权证对应标的数量，输出时除 `1000`，统一为每一
权证口径。所有 price 字段在内部使用 `double`，便于后续研究。Big5/CP950
`name` 和非十元面额 flag 不解析、不输出。

权证判断使用 format1 的 `warrant_flag`、`security_type` 和
`market_data_line`，任一判据成立即按权证字段解析；2026-07-07 完整数据中三个
判据完全一致：37,937 行均同时满足，另有 2,904 行均不满足。depth 的既有
filter mode 语义不变。

### 控制记录、重复与冲突

- `stock_entries` 为空才是证券记录；`AL` / `NE` 是循环控制记录，不写 CSV。
- dump 可能从发布循环中段开始。每个 market 的首个 `AL` / `NE` 只用于建立
  周期边界；从下一完整周期起，控制记录中的 ASCII 数量必须等于该周期实际收到
  的 format1 正常记录数，否则转换失败。
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
- 每个 symbol 维护 previous close、涨跌停、open、last、累计 volume 和
  Orion 增量估算口径的 `total_value`。
- legacy CSV 仍为 23 列，不包含 bid / ask volume；price 和
  `total_value` 保留两位小数。
- offline `symbol_id = -1`，`localtime = exchtime`。

## 有意不同于 Orion 的行为

| 项目 | Orion `4282286` | Aries | 原因与影响 |
|---|---|---|---|
| 输入配置 | TOML；`trading_day` 缺省时取当天 | CLI 直接要求 `--trading-day`、`--dump`，非 dry-run 要求 `--output` 与 `--basic-output` | 让 offline 任务参数显式、可复现 |
| 交易日起点 | Nova `mktime`，依赖进程时区 | 固定 UTC+8 自然日零点 | 避免服务器时区改变 timestamp |
| wire 表达 | packed struct、bitfield、`reinterpret_cast` | byte span、显式 offset 和 mask | 消除 ABI、对齐和 bitfield 顺序依赖 |
| frame 校验 | 按 header length 读取，不核对 XOR、terminal 或最小 trailer | 每条消息校验长度、XOR checksum、`0D 0A` terminal | 损坏输入失败，不静默生成 CSV |
| BCD 校验 | nibble 直接参与计算 | 拒绝非十进制 nibble、非法时间与溢出 | 防止错误数字进入时间和价格 |
| 协议版本 | 不检查 `service_type` / `format_version` | 只接受 service 01/02；已处理 format 只接受文档版本 9/4/1 | 新版 layout 未审查前不静默误解码 |
| 动态档位 | 根据 bitfield 直接遍历 | 先验证最多五档及精确动态 body 长度 | 避免越界或错位读取 |
| 结束控制消息 | `000000` 会被 `warrant` / `all` filter 接受，随后把 `999999999999` 当时间 | 所有 mode 都先忽略协议定义的结束控制消息 | 修复 `warrant` / `all` 完整 dump 末尾失败；有效行情不变 |
| mode 拼写 | 未知字符串退回 `stock` | CLI 和 parser 都拒绝未知 mode | 防止拼写错误生成错误资产池 |
| 输出发布 | writer 直接打开目标文件 | 同目录 `.partial.<pid>` 写完并成功 close 后 rename；默认拒绝覆盖，`--overwrite` 才替换 | 失败时不发布半文件，降低误覆盖风险 |
| 异常上下文 | 多数解析路径没有输入 byte offset | 错误包含 message 起始 byte offset | 便于定位损坏 dump |
| format1 输出 | 只更新 depth 内部状态 | 另生成 30 列、去重且排序的 basic-info CSV | 保留可研究的证券与权证基础资料 |
| format1 控制 | `AL` / `NE` 可能成为 pseudo symbol | 识别控制记录，并在首轮同步后校验每个完整周期数量 | 防止计数记录污染证券状态并检测丢包 |
| format1 重复 | 同 symbol 后到状态覆盖 | 相同记录去重；字段变化即失败并报告 diff | format1 无安全的 last-wins 时间语义 |
| CLI 日志 | 工具原有输出方式 | outer CLI 使用 Nova `INFO` / `ERROR`；core 通过异常传递 | 错误只记录一次且保持统一日志设施 |

上述差异不改变有效 2026-07-07 `stock` 数据的 legacy CSV：完整 Aries 输出
与 Orion 输出 bytes、行数及 SHA-256 相同，`cmp` 返回 0。严格校验主要改变
损坏数据、未知协议版本、错误配置和结束控制消息的处理方式。

## 当前验证边界

- 上市和上柜 format 1 offset 分别有 fixture；TPEx format 1 + format 6 已有
  端到端 CSV 测试。
- format 6 / 17 / 22 / 23、五种 mode、checksum、trailer、截断、超过五档、
  原子发布和 symlink 边界均有 focused tests。
- 2026-07-07 stock dump 含 listed 与 OTC stream，严格 checksum / version /
  service 校验下可完整读取 25,993,761 条消息并生成 15,886,026 条数据行。
- 同一 dump 含 1,145,472 条 format1 正常记录与 167 条控制记录；去除
  1,104,631 条完全相同的重复后得到 40,841 个唯一主键，未发现字段变化冲突。
- basic-info CSV 为 5,118,361 bytes、40,841 个数据行、30 列，SHA-256 为
  `093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a`；
  其中 TWSE 30,488 行、TPEx 10,353 行。
- 该 dump 没有可供 `odd_lot` format23 / `warrant` format17 depth 输出的真实
  消息；这两个 depth 输出路径目前仍由 synthetic fixture 覆盖。
- legacy CSV 丢弃五档 volume。包含 volume 的正式研究 schema 需要单独设计、
  版本化和迁移，不能直接修改本兼容输出。

## 输出实现边界

- depth 与 basic-info 都使用同步 `ofstream` writer。Aquila 使用的
  `quill::CsvWriter` 是异步 `append_row()`，无法在调用点提供与当前 writer
  等价的磁盘写入错误传播，因此本工具只借鉴 compile-time schema 的组织思想，
  不采用异步 writer。
- 两个输出均先写同目录 partial，完成后才进入发布；`--overwrite` 会先把两份旧
  输出移到 backup，任一 rename 在进程内失败时回滚新文件并恢复旧文件。
- 两次 final rename 不是文件系统级事务。进程被强杀、主机崩溃或断电发生在两次
  rename 之间时，无法保证跨文件原子性；下游应在任务成功退出后再消费两个文件，
  后续 manifest 设计应补充跨文件 commit marker / generation id。
- format22 仍只服务 odd-lot depth 的内部基础状态，本轮没有并入 basic-info CSV。
