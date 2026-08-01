# Data Converter 验证说明

更新时间：2026-07-31

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
  截断 dump 和结束控制 symbol。
- depth / basic-info CSV 内容、multiplier 与 odd-lot value 单位、dry-run、默认
  拒绝覆盖、双文件旧版本保护、partial symlink / 目录防护和发布回滚。

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
messages=25993761 depth_rows=15886026 depth_symbols=1979
basic_messages=1145472 basic_controls=167 basic_duplicates=1104631
basic_rows=40841 bytes=3153093917 dry_run=true
```

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

2026-08-01 以前可与 Orion depth 基准比较：

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
| depth | 2,742,684,274 | 15,886,026 | 23 | `f5981991517c24d07fbe4ee2ef38d9b9d3d198b69d2c841cdab39d5a8cb3cc41` |
| basic-info | 5,118,361 | 40,841 | 30 | `093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a` |

当前 depth `total_value` 已纳入 format1 `multiplier`，不得再要求 depth hash 或
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
