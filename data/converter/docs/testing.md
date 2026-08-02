# Data Converter 验证说明

更新时间：2026-08-01

## TWSE Focused Tests

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg

cmake --build --preset debug --target twse_converter_tests
ctest --test-dir build/debug \
  --output-on-failure \
  -R '^(BasicInfoDecoderTest|BasicInfoCatalogTest|BcdDecoderTest|MessageDecoderTest|DumpConverterTest)'
```

测试覆盖：

- TWSE / TPEx format1 全字段、权证单位缩放、非适用空值、`AL` / `NE` 首轮
  同步与完整周期计数、相同重复去重和字段变化冲突。
- BCD integer、decimal、exchange time 和非法 digit / time。
- header、TWSE listed / TPEx service、format 1 / 6 / 17 / 22 / 23、协议
  version、五种 filter mode 和 UTC+8 `trading_day`。
- 非法 service / version / BCD / checksum / 日期、short body、超过五档、
  截断 dump、有界 frame 重同步、受影响 symbol 隔离和结束控制 symbol。
- orderbook / basic-info CSV 内容、multiplier 与 odd-lot value 单位、dry-run、默认
  拒绝覆盖、缺失 multiplier 隔离、best-effort 双文件发布、双文件旧版本保护、
  partial symlink / 目录防护和发布回滚。

## 完整 Dump Dry-run

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /home/liuxiang/data/raw/stock/twse_stock_20260707.dump \
  --trading-day 20260707 \
  --symbol-filter-mode stock \
  --dry-run
```

成功日志应包含：

```text
status=validated messages=25993761 orderbook_rows=15886026 orderbook_symbols=1979
basic_messages=1145472 basic_controls=167 basic_duplicates=1104631
basic_rows=40841 cycle_mismatches=0 frame_errors=0
missing_multiplier_messages=0 bytes=3153093917 dry_run=true
```

## TWSE 历史批量重建

待处理日期文件每行一个 `YYYYMMDD`。runner 优先使用 `/tw_backup` 中已存在的非空
dump；否则校验并解压 `/data/tw/raw/stock` 中的归档，解压后的 dump 保留。示例：

```bash
data/converter/scripts/rebuild_twse_csv \
  --converter "$PWD/build/release/data/converter/twse_dump_converter" \
  --dates-file /home/liuxiang/tmp/<run>/dates.txt \
  --run-dir /home/liuxiang/tmp/<run>/logs
```

默认输出到 `/tw_backup/data/tw/csv/stock/`。summary 每个日期只写一行，并分别记录
`result` 与 `publication`；成功必须同时满足 converter exit status 为 0、本轮两份
输出 inode 都发生变化以及日志含 `published_complete` 或 `published_partial`。
已有输出未变化只能记为 `preserved_previous`。

零字节 dump 需要通过 `--non-trading-days-file` 提供已经确认的休市日期清单。清单中
的日期记为 `non_trading_day`；其他零字节输入保守记为 `empty_input`。损坏归档记为
`input_corrupt`，不覆盖已有 CSV，批处理继续下一日。

## 历史 Orion 对照与当前验证

完整转换结果写入 `/home/liuxiang/tmp`，同时指定两份输出：

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /home/liuxiang/data/raw/stock/twse_stock_20260707.dump \
  --trading-day 20260707 \
  --symbol-filter-mode stock \
  --output /home/liuxiang/tmp/<run>/twse_stock_20260707.csv \
  --basic-output /home/liuxiang/tmp/<run>/twse_basic_info_20260707.csv
```

2026-08-01 以前可与 Orion orderbook 基准比较：

```bash
sha256sum \
  /home/liuxiang/tmp/<run>/twse_stock_20260707.csv \
  /home/liuxiang/data/csv/stock/twse_stock_20260707.csv

cmp \
  /home/liuxiang/tmp/<run>/twse_stock_20260707.csv \
  /home/liuxiang/data/csv/stock/twse_stock_20260707.csv
```

2026-07-07 修改前的历史验证基线：

| 输出 | bytes | 数据行 | 列数 | SHA-256 |
|---|---:|---:|---:|---|
| orderbook | 2,742,684,274 | 15,886,026 | 23 | `f5981991517c24d07fbe4ee2ef38d9b9d3d198b69d2c841cdab39d5a8cb3cc41` |
| basic-info | 5,118,361 | 40,841 | 30 | `093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a` |

当前 orderbook `total_value` 已纳入 format1 `multiplier`，不得再要求 orderbook hash 或
`cmp` 与 Orion 相同。完整验证应保持数据行数和 23 列 schema，并抽查一般交易满足
`new_total_value - old_total_value = delta_volume * last_price * multiplier`；
odd-lot 使用有效乘数 1。

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

完整文件检查还应覆盖：列数与 header 一致、symbol 非空、`symbol_id = -1`、
`exchtime` / `localtime` 为整数，且 `exchtime` 位于配置的本地交易日窗口。
当前仓库尚未把这组检查封装成可复用命令。

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
原子发布。

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
