# Data Converter 验证说明

更新时间：2026-08-04

## TWSE Focused Tests

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg

cmake --build --preset debug --target twse_converter_tests
ctest --test-dir build/debug \
  --output-on-failure \
  -R '^(BasicInfoDecoderTest|BasicInfoCatalogTest|BcdDecoderTest|OrderbookTest|MessageDecoderTest|DumpConverterTest)'
```

测试覆盖：

- TWSE / TPEx format1 全字段、权证单位缩放、非适用空值、`AL` / `NE` 首轮
  同步与完整周期计数、相同重复去重和字段变化冲突。
- BCD integer、decimal、exchange time 和非法 digit / time。
- header、TWSE listed / TPEx service、format 1 / 6 / 17 / 22 / 23、协议
  version、五种 filter mode 和 UTC+8 `trading_day`。
- 非法 service / version / BCD / checksum / 日期、short body、超过五档、
  截断 dump、有界 frame 重同步、受影响 symbol 隔离和结束控制 symbol。
- 股票 POD/state accessor、原子 multi-trade group、interleaved symbol、held、trial、
  source-format 分组边界、buy/sell/unknown、OHLC、missing-volume checkpoint、
  market identity、format23 reserved bit。
- 37 列 orderbook / 30 列 basic-info CSV 内容、multiplier 与 odd-lot value 单位、dry-run、默认
  拒绝覆盖、缺失 multiplier 隔离、best-effort 双文件发布、双文件旧版本保护、
  partial symlink / 目录防护和发布回滚。
- 测试 binary 显式初始化 Nova backend；端到端 fixture 对迁移后的 Quill writer 做
  blocking flush/close 后逐 byte CSV 对照，锁定 `twse-orderbook-v2` schema 与数值格式。

2026-08-04 最终验证：Debug 完整 CTest 83/83 通过；独立
ASan/UBSan build 中 TWSE binary 53/53 通过，无 sanitizer 报告。

## 完整 Dump Dry-run

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /tw_backup/data/tw/raw/stock/twse_stock_20260707.dump \
  --trading-day 20260707 \
  --symbol-filter-mode all \
  --dry-run
```

成功日志应包含：

```text
status=validated schema=twse-orderbook-v2 local_time_source=exchange_fallback
messages=25993761 orderbook_rows=24458643 source_actual_trades=2312789
actual_trades=2312789 match_groups=2254614 multi_trade_groups=45270
trades_in_multi_groups=103445 held_ended_groups=78
buy_groups=1106454 sell_groups=1109972 unknown_groups=38188
basic_messages=1145472 basic_controls=167 basic_duplicates=1104631
basic_rows=40841 cycle_mismatches=0 frame_errors=0 sequence_gaps=0
incomplete_groups=0 value_imputations=0
missing_multiplier_messages=0 bytes=3153093917 dry_run=true
```

最终 Release dry-run wall time 为 5.80 秒，maximum RSS 333,760 KiB；该数值只是
当前机器与本次 page-cache 状态下的实测，不作为性能提升结论。最终日志位于
`/home/liuxiang/tmp/twse_atomic_20260707_final_dry_run.log`。

## TWSE 历史批量重建

待处理日期文件每行一个 `YYYYMMDD`。runner 优先使用 `/tw_backup` 中已存在的非空
dump；否则校验并解压 `/data/tw/raw/stock` 中的归档，解压后的 dump 保留。示例：

```bash
data/converter/scripts/rebuild_twse_csv \
  --converter "$PWD/build/release/data/converter/twse_dump_converter" \
  --dates-file /home/liuxiang/tmp/<run>/dates.txt \
  --run-dir /home/liuxiang/tmp/<run>/logs
```

默认输出到 `/tw_backup/data/tw/csv/stock/`。summary 每个日期只写一行，并记录
`result`、`publication`、`converter_sha256`、`schema=twse-orderbook-v2`、
`orderbook_bytes` 与 `basic_bytes`；每日 log 也记录 converter path/hash。成功必须
同时满足 converter exit status 为 0、本轮两份
输出 inode 都发生变化、日志含 `published_complete` 或 `published_partial`、
完成日志声明 v2 schema，且输出 exact header 实际识别为 v2。summary 的
`schema` 是从文件 header 检测的结果，不是无条件常量；已发布但 schema 不符时
记为 `published_unexpected_schema/runner_error`。
已有输出未变化只能记为 `preserved_previous`。

零字节 dump 需要通过 `--non-trading-days-file` 提供已经确认的休市日期清单。清单中
的日期记为 `non_trading_day`；其他零字节输入保守记为 `empty_input`。损坏归档记为
`input_corrupt`，不覆盖已有 CSV，批处理继续下一日。

### 2026-08-04 最近五个自然日重建

用 `main` 的 Release converter 处理 2026-07-30 至 2026-08-03。converter binary
SHA-256 为
`b7ae6db23df5f789f39567c77a88920ba4fdecc40b2d6e6aab23a4c4adf72859`。

| 日期 | 结果 | Orderbook 行 | bytes | sequence gap | value imputation | cycle mismatch |
|---|---|---:|---:|---:|---:|---:|
| 2026-07-30 | `published_partial` | 19,055,457 | 4,515,710,410 | 222 | 99 | 1 |
| 2026-07-31 | `published_partial` | 9,759,236 | 2,295,714,892 | 46 | 15 | 0 |
| 2026-08-01 | `missing_input` | 0 | 0 | 0 | 0 | 0 |
| 2026-08-02 | `missing_input` | 0 | 0 | 0 | 0 | 0 |
| 2026-08-03 | `published_partial` | 11,652,668 | 2,762,822,991 | 61 | 17 | 1 |

三个交易日均为 `twse-orderbook-v2` exact header，独立 `wc -l` 与 converter
summary 严格一致；frame error、incomplete group、missing multiplier 均为 0。
2026-08-01/02 没有 raw dump 或 archive，因此未生成空 CSV。六个已发布文件的
SHA-256、basic-info 行数与逐日日志见：

```text
/home/liuxiang/tmp/twse-v2-last5-20260804/final_summary.tsv
```

2026-07-30 原有 dump 为 `0600`，受 NFS 身份映射影响无法读取。旧文件已保留为
`twse_stock_20260730.dump.unreadable-20260804T1315`；从同日已通过 `gzip -t`
的 archive 重新解压出 4,057,158,258-byte、`0664` dump 后转换成功。

## 历史 Orion 基线与 v2 验证

完整转换结果写入 `/home/liuxiang/tmp`，同时指定两份输出：

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /tw_backup/data/tw/raw/stock/twse_stock_20260707.dump \
  --trading-day 20260707 \
  --symbol-filter-mode stock \
  --output /home/liuxiang/tmp/<run>/twse_stock_20260707.csv \
  --basic-output /home/liuxiang/tmp/<run>/twse_basic_info_20260707.csv
```

2026-07-07 修改前的 Orion-compatible 23 列历史基线只用于识别 legacy 文件：

| 输出 | bytes | 数据行 | 列数 | SHA-256 |
|---|---:|---:|---:|---|
| orderbook | 2,742,684,274 | 15,886,026 | 23 | `f5981991517c24d07fbe4ee2ef38d9b9d3d198b69d2c841cdab39d5a8cb3cc41` |
| basic-info | 5,118,361 | 40,841 | 30 | `093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a` |

v2 不保留 legacy writer，不能再要求 orderbook 行数、hash 或 bytes 与 Orion 相同。
2026-07-07 `stock` 完整输出实际基线：

| 输出 | bytes | 数据行 | 列数 | SHA-256 |
|---|---:|---:|---:|---|
| orderbook v2 | 3,705,213,634 | 15,548,031 | 37 | `28adc5806ce2a8315b4927e94af85812924afc3b807a719cc3dcf72ffd3428be` |
| basic-info | 5,118,361 | 40,841 | 30 | `093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a` |

最终 Release 转换耗时 18.92 秒，maximum RSS 348,116 KiB。orderbook 聚合计数为
`actual_trades=1,906,696`、`match_groups=1,855,576`、
`multi_trade_groups=39,238`、`trades_in_multi_groups=90,358`、
`held_ended_groups=78`，且 gap/incomplete/imputation 均为 0。
本次 Release converter binary SHA-256 为
`9a8283a8e7744f699444412d37516d0e756683d9b5da9104b39b101d68371cc5`。

basic-info 还应检查：header 与每行均为 30 列；主键严格递增且唯一；TWSE 的
`stock_group_code` 为空、TPEx 的 `foreign_stock_flag` 为空；非权证的权证专属
列为空；price 固定 4 位、`exercise_ratio` 固定 5 位、`maturity_date` 为 ISO
日期。2026-07-07 实际分布为 TWSE 30,488 行、TPEx 10,353 行、权证
37,937 行、非权证 2,904 行。

一次性转换至少记录输入、工具版本、config、输出路径、bytes、数据行数、列数、
时间范围和 SHA-256。2026-07-07 的结果与边界见 `data/docs/data.md`。

检查已发布文件大小与 hash：

```bash
stat -c '%n %s bytes %y' \
  /home/liuxiang/data/csv/future/taifex_20260707_stock_future.csv \
  /home/liuxiang/data/csv/stock/twse_stock_20260707.csv

sha256sum \
  /home/liuxiang/data/csv/future/taifex_20260707_stock_future.csv \
  /home/liuxiang/data/csv/stock/twse_stock_20260707.csv
```

完整文件独立扫描至少覆盖：exact header 与每行 37 列、symbol 非空、market/state/side
范围、`local_ns=exchange_ns` fallback、held 行全部盘口为 0、`trade_count=0` 时
`trade_side=unknown` 且 `trade_volume=0`、每个 `(market,symbol)` 的累计量额不回退、
每个 market 的 `source_sequence` 严格递增、所有 decimal 固定 4 位。2026-07-07 的
15,548,031 行已全部通过上述检查，聚合 group/trade/side 计数与 converter summary 一致。

## TAIFEX Focused Tests

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg

cmake --build --preset debug --target taifex_converter_tests
ctest --test-dir build/debug --output-on-failure -R 'Taifex|taifex'
```

测试覆盖 BCD/header、I010/I011、outright/spread metadata、multiplier、I024 多笔
成交、negative spread 的非负 `total_value`、I012 涨跌停解析/重复/冲突、I025
high/low、I081 顺序更新、I083 全量清空、I084 gap recovery、I002 reset、零价
spread 开盘、snapshot statistics 防回滚/安全合并、未恢复 gap 与永久 metadata
缺失的问题摘要/继续发布、单 symbol cache overflow 隔离、checksum、truncated
trailer、13:45:59 inclusive / 13:46 exclusive 日盘硬截止、dry-run 和双 CSV
原子发布。端到端 fixture 对 Quill writer 的 44 列 orderbook 和 27 列 basic-info
输出做逐 byte 对照。

## TAIFEX 完整 Dump 验证

```bash
./build/release/data/converter/taifex_dump_converter \
  --dump /tw_backup/data/tw/raw/future/taifex_20260707.dump \
  --trading-day 20260707 \
  --dry-run

./build/release/data/converter/taifex_dump_converter \
  --dump /tw_backup/data/tw/raw/future/taifex_20260707.dump \
  --trading-day 20260707 \
  --output /tw_backup/data/tw/csv/future/taifex_20260707_future.csv \
  --basic-output /tw_backup/data/tw/csv/future/taifex_20260707_future_basic_info.csv \
  --overwrite
```

成功日志应包含：

```text
messages=61605862 orderbook_rows=54086067 symbols=4839
i010=154801 i011=56403 i012=154880
basic_duplicates=209085 i012_duplicates=153120 i012_conflicts=0 basic_rows=4839
ignored=2260130 metadata_missing=9 sequence_gaps=4 unresolved_gaps=0
stale=0 recoveries=4 cache_overflows=0 resets=0 bytes=5640462381
day_cutoff_reached=true day_cutoff_offset=5640462381
```

完整 CSV 另用独立扫描检查：orderbook header/每行为 44 列、basic-info 为 27 列，
`symbol_id=-1`、`localtime=exchtime`、volume/flag/high-low 合法、每个 orderbook symbol
sequence 严格递增、basic symbol 排序且唯一、multiplier 大于 0。负价格必须只出现在
calendar spread，`total_value` 必须非负。当前 contract 的全量重建输出位于
`/tw_backup/data/tw/csv/future/`；完成后再记录固定 hash 基线。

## Nova / Quill Writer 迁移验证

2026-08-02 在保持当时 `Orderbook<5>` 与 CSV schema 不变的前提下，将两个 converter
迁移到 Nova frontend 的 Quill writer。除完整 CTest 外，迁移前后的 executable 对相同
真实输入分别转换并执行 `cmp`：

| 市场 / 输入 | 消息 | orderbook 数据行 | basic-info 数据行 | 结果 |
|---|---:|---:|---:|---|
| TWSE 2026-04-06 完整 dump，429,426,360 bytes | 3,771,162 | 18,094 | 44,915 | 两份 CSV 均逐 byte 相同 |
| TAIFEX 2026-02-11 frame-aligned 前缀，67,109,260 bytes | 147,715 | 23 | 1,753 | 两份 CSV 均逐 byte 相同 |

对应 SHA-256：

| 输出 | SHA-256 |
|---|---|
| TWSE orderbook | `e418d8a570e598ddb53772938d59dd90d2c036a0777c99fdd8f5a9df1b6ce99c` |
| TWSE basic-info | `d30463b60d01024b0c24349a504bb91a131a1849fda48d78de830b0e28624977` |
| TAIFEX orderbook | `2a5d7b09e46745d12f8dd8dbc1fc1439bcb3856b15460f603ad978b7a1672fab` |
| TAIFEX basic-info | `6ba707e3489849f7e8b9babcd0586961d32ab9a8a9a039d6dd852be97ed887f1` |

验证产物保存在 `/home/liuxiang/tmp/aries-quill-rows-smoke.jMbMOX/`；它们是 scratch
结果，不进入 git，也不作为长期数据版本事实源。
