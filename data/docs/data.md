# 数据说明

更新时间：2026-08-01

## 当前范围

当前已有工作集中在台湾 raw 行情下载、本地落盘、TWSE / TAIFEX dump converter，
以及 2026-07-07 真实数据验证。TWSE 当前生成 23 列 depth 与 format1 basic-info；
TAIFEX 生成全 futures research depth/basic-info。仓库尚未建立跨市场统一的 schema
version、manifest、分区格式、夜盘交易日历或通用质量报告。

## NAS Raw 下载

仓库通过 `scripts/` 提供：

- `scripts/pull-tw-raw`：统一入口脚本，当前支持 `future` 和 `stock`，后续可扩展 `etf`。
- `scripts/synology-pull`：通过 Synology File Station API 下载文件的通用 Python 程序。
- `scripts/README.md`：脚本使用说明。

`pull-tw-raw` 的重要行为：

- 默认下载根目录为 `/data/tw/raw`。
- `future` 写入 `/data/tw/raw/future`，远端目录为 `/taiwan_stock/FubunData/future_tick_raw`。
- `stock` 写入 `/data/tw/raw/stock`，远端目录为 `/taiwan_stock/FubunData/stock_tick_raw`。
- 如果本地目标文件存在且大小与远端一致，认为下载完成并跳过。
- 未完成文件使用 `.part` 后缀，下次运行会尝试断点续传。
- 同一数据类型同一时间通过 lock 限制为单任务。
- `--today` 默认按 `Asia/Taipei` 日期计算；当前服务器时区为北京时间，与台湾时间一致。
- 达到远端大小的完整 `.part` 会直接原子发布，不再发起空 Range 请求。
- 拒绝使用 symlink 或其他非普通文件作为 `.part`，避免续传覆盖目录外文件。
- NAS URL 和账号不写入公开仓库，运行环境必须设置 `SYNOLOGY_URL` 和
  `SYNOLOGY_USER`；密码只从权限为 `0600` 或更严格的本地文件读取。

当前服务器 crontab 已在本机配置 `SYNOLOGY_URL` 和 `SYNOLOGY_USER`，值不写入
仓库。任务部分为：

```cron
10 15 * * * /home/liuxiang/dev/aries/scripts/pull-tw-raw --type future --today >> /data/log/crontab/future_daily.log 2>&1
10 15 * * * /home/liuxiang/dev/aries/scripts/pull-tw-raw --type stock --today >> /data/log/crontab/stock_daily.log 2>&1
30 15 * * * /home/liuxiang/dev/aries/scripts/pull-tw-raw --type future --today >> /data/log/crontab/future_daily.log 2>&1
30 15 * * * /home/liuxiang/dev/aries/scripts/pull-tw-raw --type stock --today >> /data/log/crontab/stock_daily.log 2>&1
```

日志目录：

```text
/data/log/crontab/
```

## 当前本地数据快照

截至 2026-07-31T17:51:13+08:00：

```text
/data/tw/raw/future/taifex_20260729.dump.tar.gz
/data/tw/raw/future/taifex_20260730.dump.tar.gz
/data/tw/raw/future/taifex_20260731.dump.tar.gz
/data/tw/raw/stock/twse_stock_20260729.dump.tar.gz
/data/tw/raw/stock/twse_stock_20260730.dump.tar.gz
/data/tw/raw/stock/twse_stock_20260731.dump.tar.gz
```

2026-07-31 的文件均已完成下载并通过 `gzip -t`：

| 数据类型 | 文件 | bytes |
|---|---|---:|
| future | `/data/tw/raw/future/taifex_20260731.dump.tar.gz` | 938,635,037 |
| stock | `/data/tw/raw/stock/twse_stock_20260731.dump.tar.gz` | 508,095,091 |

## TWSE Dump Converter

实现入口：

```text
data/converter/twse/
tests/data/converter/twse/
```

`twse_dump_converter` 从 dump 连续读取 10-byte header 和
`message_length - 10` byte body，只处理 Orion offline converter 当前使用的
消息：

| format | 含义 | 处理方式 |
|---:|---|---|
| 1 | stock basic info | 更新 depth 状态，并进入去重后的 basic-info CSV |
| 6 | stock depth | 更新五档与成交状态；按 mode 决定是否输出 |
| 17 | warrant depth | 复用标准 depth 解码；`warrant` mode 输出 |
| 22 | odd-lot basic info | `odd_lot` mode 更新基础状态 |
| 23 | odd-lot depth | `odd_lot` mode 输出 |

listed 使用 `service_type = 01`，TPEx / OTC 使用 `service_type = 02`；两种
市场共用同一个 CLI。format 1 根据 service 选择不同 offset，其余已处理
format 的 wire layout 相同。协议文档逐项核对和 Orion 差异见
`data/converter/docs/twse.md`。

filter mode 为 `stock`、`etf`、`warrant`、`odd_lot`、`all`。为了保持 Orion
兼容性，`stock` 沿用其四字符 symbol 判断，因此也会接受四字符 ETF；
format 6 末尾 symbol 为 `000000`、时间为 `999999999999` 的结束控制消息会
在所有 mode 中忽略。

各 mode 沿用 Orion 当前输出路径，不把不同 format 自动合并：

| mode | 输出 |
|---|---|
| `stock` | format 6 中符合四字符判断的 symbol |
| `etf` | format 6 中符合 ETF 规则的 symbol |
| `warrant` | format 17；format 6 只更新同 symbol 状态 |
| `odd_lot` | format 23 |
| `all` | format 6 的全部非控制 symbol |

depth CSV 固定为以下 23 列：

```text
symbol,symbol_id,exchtime,localtime,high_limit,low_limit,last_price,
ask_price1,bid_price1,ask_price2,bid_price2,ask_price3,bid_price3,
ask_price4,bid_price4,ask_price5,bid_price5,open,total_trade,
total_volume,total_value,status,sequence
```

format1 另生成 30 列 basic-info CSV，主键和排序键为
`(trading_day, market, symbol)`；字段、类型、权证单位缩放和空值 contract 见
`data/converter/docs/twse.md`。它不包含 Big5/CP950 证券名称。

数据和时间语义：

- `trading_day` 按 UTC+8 自然日零点计算，不依赖进程时区；消息中的 BCD
  `HHMMSSmmmuuu` 是当日偏移。
- offline `symbol_id` 固定为 `-1`，`localtime` 等于 `exchtime`。
- price 和 `total_value` 使用两位小数；五档 volume 参与内部状态更新，但当前
  23 列 CSV 不包含对应列。
- 每个 symbol 跨消息保存 high / low limit、open、last 和累计 volume。一般交易
  的 `total_value` 累计 `delta_volume * last_price * multiplier`，单位为
  basic-info `currency`；odd-lot 的有效乘数为 1。
- 非法 service / format version / BCD / message length / checksum、截断、
  非法 trailer 或超过五档会终止转换。
- 非 dry-run 必须指定不同的 `--output` 与 `--basic-output`。两份 CSV 都先写
  同目录 `.partial.<pid>`；成功 flush / close 后才进入发布。默认拒绝覆盖，
  `--overwrite` 会在进程内发布失败时恢复两份旧输出。跨两次 final rename 的
  进程崩溃 / 断电不具备文件系统事务保证。

## 2026-07-07 Dump 转 CSV

本节先保留 Orion legacy 基准，再记录 Aries converter 的独立结果。两种 TAIFEX
输出的资产池、schema 和 value 口径不同，不能直接按 bytes 比较。

输入 dump：

```text
/home/liuxiang/data/raw/future/taifex_20260707.dump
/home/liuxiang/data/raw/stock/twse_stock_20260707.dump
```

基准转换使用 `/home/liuxiang/dev/orion` 的 `main` 提交
`4282286 Merge pull request #66 from dcfintech/validation/future-spot-arbitrage-native-x86`
及其 `build/debug` 工具。实际 scratch config 和日志保留在：

```text
/home/liuxiang/tmp/aries-20260707-convert.eIxfQr/
```

实际命令：

```bash
cd /home/liuxiang/dev/orion

./build/debug/tools/taifex_stock_future_data_engine \
  --config /home/liuxiang/tmp/aries-20260707-convert.eIxfQr/taifex.toml \
  --mode to_csv \
  --dump /home/liuxiang/data/raw/future/taifex_20260707.dump

./build/debug/tools/twse_data_engine \
  --config /home/liuxiang/tmp/aries-20260707-convert.eIxfQr/twse.toml \
  --mode to_csv \
  --dump /home/liuxiang/data/raw/stock/twse_stock_20260707.dump
```

两个 config 均设置 `trading_day = 20260707`。TAIFEX config 将
`taifex_data_reader.csv_output_path` 指向显式 `.csv` 文件；TWSE config 设置
`symbol_filter_mode = "stock"`、`csv_output_path` 和
`csv_output_filename`。转换先写入 `/home/liuxiang/tmp`，通过检查后再移动到
正式目录。

输出与验证结果：

| 数据类型 | 正式输出 | bytes | 数据行 | 列数 | SHA-256 |
|---|---|---:|---:|---:|---|
| future | `/home/liuxiang/data/csv/future/taifex_20260707_stock_future.csv` | 3,517,520,520 | 16,527,099 | 40 | `cdf51fd07933cd468383819e9cdbb77cf3cde96e371931dd659bc00cac90af97` |
| stock | `/home/liuxiang/data/csv/stock/twse_stock_20260707.csv` | 2,742,684,274 | 15,886,026 | 23 | `f5981991517c24d07fbe4ee2ef38d9b9d3d198b69d2c841cdab39d5a8cb3cc41` |

检查覆盖：

- 两个转换进程均返回 `0`；TAIFEX 日志记录
  `published_event_count = 22647767` 并完成原子发布，TWSE 日志记录
  `unknown_symbol_count = 0`。
- 数据行列数均与 header 一致，没有空 symbol。
- `to_csv` 输出的 `symbol_id` 全部为 `-1`，符合 `orion` offline CSV
  contract；后续持久化或回放不能把它当作 runtime identity。
- `exchtime` 均为整数且位于 2026-07-07 本地交易日窗口
  `[1783353600000000000, 1783440000000000000)`；future 实际范围为
  `1783384205000000000` 至 `1783403158019000000`，stock 实际范围为
  `1783384200073709000` 至 `1783402380000000000`。
- 两份文件发布后重新计算 SHA-256，结果见上表。

以下是 2026-08-01 修改 `total_value` multiplier contract 前的历史兼容性验证。
当时 Aries converter 使用相同 stock dump 和 `trading_day = 20260707` 做完整转换：

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /home/liuxiang/data/raw/stock/twse_stock_20260707.dump \
  --output /home/liuxiang/tmp/<run>/twse_stock_20260707.csv \
  --basic-output /home/liuxiang/tmp/<run>/twse_basic_info_20260707.csv \
  --trading-day 20260707 \
  --symbol-filter-mode stock
```

读取 25,993,761 条 message、输出 15,886,026 条 depth 数据行、维护 1,979 个
depth symbol，共读取 3,153,093,917 bytes。Aries depth 输出与 Orion 正式输出均为
2,742,684,274 bytes，SHA-256 均为
`f5981991517c24d07fbe4ee2ef38d9b9d3d198b69d2c841cdab39d5a8cb3cc41`，
`cmp` 返回 0。新 contract 不再以该 depth hash 或 `cmp` 为完成条件。完整 dump 的
其余四种 filter mode 也已通过 dry-run；该 dump
没有可供 `odd_lot` format23 / `warrant` format17 depth 输出的真实消息，
因此这两种 depth 输出路径仍以 synthetic fixture 为验证证据。

同次转换读取 1,145,472 条 format1 正常记录和 167 条控制记录；去除
1,104,631 条完全相同的重复后，basic-info 得到 40,841 个唯一主键，未发现
同键字段变化。输出为 5,118,361 bytes、40,841 个数据行、30 列，SHA-256 为
`093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a`。
主键严格排序且唯一；TWSE 30,488 行、TPEx 10,353 行；37,937 条权证的
`warrant_flag`、`security_type` 与 `market_data_line` 三个判据完全一致，另有
2,904 条非权证。验证输出位于：

```text
/home/liuxiang/tmp/aries-twse-basic-verify.w0V22Y/twse_stock_20260707.csv
/home/liuxiang/tmp/aries-twse-basic-verify.w0V22Y/twse_basic_info_20260707.csv
```

### Aries TAIFEX 全 Futures 输出

输入使用：

```text
/data/tw/raw/future/taifex_20260707.dump.tar.gz
/tw_backup/data/tw/raw/future/taifex_20260707.dump
```

归档为 1,213,053,544 bytes，唯一 member 解压后为 6,005,844,926 bytes。解压文件
保留在 `/tw_backup/data/tw/raw/future/`；原始压缩包也保留不变。

当前 44 列/non-negative value contract 的完整 dry-run：

```bash
./build/release/data/converter/taifex_dump_converter \
  --dump /tw_backup/data/tw/raw/future/taifex_20260707.dump \
  --trading-day 20260707 \
  --dry-run
```

converter 完整读取 62,559,197 条消息，模拟输出 54,221,272 条 depth 数据行；处理
181,201 条 I010、66,096 条 I011 与 181,280 条 I012。I010/I011 一致重复为
245,178 条，I012 一致重复为 179,520 条、冲突为 0，basic-info 为 4,839 条。
9 条 realtime 在 metadata 前到达；4 个 product sequence gap 全部恢复，没有 stale、
EOF 未恢复 gap 或 cache overflow。ignored 总数为 2,315,494，并按
transmission/message kind 分项记录。

metadata-before-basic 涉及 `TJFH6/L6`、`TJFL6`、`TJFG6`；gap 涉及
`TJFG6`、`TJFL6`、`TJFH6`、`TJFH6/L6`。完整 dry-run 成功，wall time
1:53.56，最大 RSS 270,316 KiB。

下表是 2026-08-01 schema/value 修改前的历史 Aries 输出，不是当前 44 列、
`abs(price) * volume * multiplier` contract 的 hash 基线：

| 输出 | bytes | 数据行 | 列数 | SHA-256 |
|---|---:|---:|---:|---|
| `/data/tw/csv/future/taifex_20260707_future.csv` | 18,034,209,936 | 54,221,272 | 45 | `5dd579b47fd1b0ee803c076d36cef58e14631741c9672051283ca16a228688f1` |
| `/data/tw/csv/future/taifex_20260707_future_basic_info.csv` | 487,542 | 4,839 | 27 | `200f3c86d6f788a99f23d733f4ccdd28c91044085963c4d4c32b9ea0b6094577` |

历史输出的独立全文件检查确认 depth 每行 45 列、2,923 个有 depth 的 symbol 内 sequence
严格递增，identity/time/volume/flag/high-low 约束无异常；basic-info 每行 27 列、
symbol 排序且唯一、multiplier 全部大于 0。43,388 行包含负 signed price/value，
全部来自 calendar spread。发生 gap 的 4 个 symbol 共 441,094 行持续标记
`continuous_flag=0`，防止下游把无法完整重建的累计 `total_value` 当作完整 history。

当前全量任务会把 44/27 列结果写入 `/tw_backup/data/tw/csv/future/`；每个交易日
独立记录 success/failure 和 symbol 级问题，完成后再建立当前 contract 的 manifest
与 hash 基线。TAIFEX schema、multiplier/value 口径、恢复语义、Orion 差异和夜盘边界见
`data/converter/docs/taifex.md`。

TWSE 历史重建采用 `data/converter/scripts/rebuild_twse_csv`，raw dump 保留在
`/tw_backup/data/tw/raw/stock/`，CSV 成对写入 `/tw_backup/data/tw/csv/stock/`。
2026-08-01 已验证并发布三个旧失败日：

| 日期 | publication | depth 行 | basic-info 行 | 问题 |
|---|---|---:|---:|---|
| 2025-09-09 | `published_partial` | 8,861,899 | 45,380 | 3 个 format1 cycle mismatch |
| 2025-09-25 | `published_partial` | 4,075,957 | 45,657 | offset `797405152` 跳过 32 bytes；`2603` 后续隔离 |
| 2025-10-14 | `published_partial` | 2,541,590 | 45,790 | offset `554258381` 跳过 51 bytes；`1256` 后续隔离 |

三天 depth 全文件检查均为 23 列、`total_value` 非负且按 symbol 不回退；basic-info
均为 30 列、主键无重复且 multiplier 全部大于 0。局部损坏后的受影响 symbol 不再
输出，因此该 symbol 的当日文件只包含损坏前的可信前缀；研究时必须结合逐日日志和
publication status 使用。

## 未完成事项

- 为 raw、dump、csv、后续 parquet / binary 研究数据确定统一目录约定，避免 `/data/tw/raw` 与 `/home/liuxiang/data/raw` 长期并存而语义不清。
- 建立数据 manifest：数据类型、交易日、来源、远端路径、本地路径、大小、hash、生成命令、生成时间和质量检查状态。
- TAIFEX 当前只允许正式转换日盘。夜盘支持 deferred；未来启用前必须建立交易日到
  事件自然日的交易日历映射，并补真实夜盘回归和 timestamp 一致性检查。
- 为 TAIFEX/TWSE 输出建立 manifest 与明确 schema version，记录输入 hash、工具
  commit、输出 hash、质量统计和交易日/symbol 问题摘要。
- 为 TWSE 设计包含 bid / ask volume 的正式版本化 schema；在 contract、
  manifest 和迁移规则锁定前，不把当前 23 列 CSV 当作正式研究数据集。
