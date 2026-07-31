# TWSE / TPEx Dump Converter 设计与 Orion 差异

更新时间：2026-07-31T17:51:13+08:00

## 事实源与范围

本实现以以下三项为事实源：

- `docs/exchange/TWSE集中市場即時交易資訊傳輸規格書(B.12.11)(202503)_20250113092444.pdf`：
  上市股票，业务别 `01`。
- `docs/exchange/上櫃股票IP行情網路規格書(V.12.16 TCPIP).pdf`：上柜股票，
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
| 输入配置 | TOML；`trading_day` 缺省时取当天 | CLI 直接要求 `--trading-day`、`--dump`，非 dry-run 要求 `--output` | 让 offline 任务参数显式、可复现 |
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
- 该 dump 没有可供 `odd_lot` / `warrant` 输出的真实消息；这两个输出路径目前
  仍由 synthetic fixture 覆盖。
- legacy CSV 丢弃五档 volume。包含 volume 的正式研究 schema 需要单独设计、
  版本化和迁移，不能直接修改本兼容输出。
