# Data Converter 验证说明

更新时间：2026-07-31

## TWSE Focused Tests

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg

cmake --build --preset debug --target twse_converter_tests
ctest --test-dir build/debug \
  --output-on-failure \
  -R '^(BcdDecoderTest|MessageDecoderTest|DumpConverterTest)'
```

测试覆盖：

- BCD integer、decimal、exchange time 和非法 digit / time。
- header、TWSE listed / TPEx service、format 1 / 6 / 17 / 22 / 23、协议
  version、五种 filter mode 和 UTC+8 `trading_day`。
- 非法 service / version / BCD / checksum / 日期、short body、超过五档、
  截断 dump 和结束控制 symbol。
- legacy CSV 内容、dry-run、默认拒绝覆盖、partial symlink 防护和原子发布。

## 完整 Dump Dry-run

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /home/liuxiang/data/raw/stock/twse_stock_20260707.dump \
  --trading-day 20260707 \
  --symbol-filter-mode stock \
  --dry-run
```

## Orion 兼容性比较

完整转换结果写入 `/home/liuxiang/tmp`，再与 Orion 基准比较：

```bash
sha256sum \
  /home/liuxiang/tmp/<run>/twse_stock_20260707.csv \
  /home/liuxiang/data/csv/stock/twse_stock_20260707.csv

cmp \
  /home/liuxiang/tmp/<run>/twse_stock_20260707.csv \
  /home/liuxiang/data/csv/stock/twse_stock_20260707.csv
```

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
