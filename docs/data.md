# 数据说明

更新时间：2026-07-31T15:42:19+08:00

## 当前范围

当前已有工作集中在台湾 raw 行情下载、本地落盘和 2026-07-07 dump 到
CSV 的一次性转换。仓库尚未建立正式数据 schema、manifest、分区格式、
质量检查报告或转换后的研究数据集 contract；本页记录的是当前操作事实，
不能替代后续数据 contract。

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

截至 2026-07-31T15:42:19+08:00：

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

## 2026-07-07 Dump 转 CSV

输入 dump：

```text
/home/liuxiang/data/raw/future/taifex_20260707.dump
/home/liuxiang/data/raw/stock/twse_stock_20260707.dump
```

转换使用 `/home/liuxiang/dev/orion` 的 `main` 提交
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

## 未完成事项

- 为 raw、dump、csv、后续 parquet / binary 研究数据确定统一目录约定，避免 `/data/tw/raw` 与 `/home/liuxiang/data/raw` 长期并存而语义不清。
- 建立数据 manifest：数据类型、交易日、来源、远端路径、本地路径、大小、hash、生成命令、生成时间和质量检查状态。
- 把当前一次性 `orion` scratch config 收敛为仓库内可复用、可参数化且带
  dry-run 的转换入口；在入口和输出 schema 锁定前，不把本轮 CSV 视为正式
  版本化研究数据集。
