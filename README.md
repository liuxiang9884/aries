# aries

`aries` 是日内策略研究仓库。当前代码模块按业务领域组织，数据 dump 转换入口
位于 `data/converter/`。

## 构建

项目要求 CMake 3.28、Ninja 和 vcpkg。首次配置前设置 vcpkg 根目录：

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

本地开发 Nova 时，可以覆盖固定的远端源码：

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg
cmake --preset debug \
  -DFETCHCONTENT_SOURCE_DIR_NOVA=/home/liuxiang/dev/nova
```

仓库提交 `CMakePresets.json` 和 `vcpkg.json`；机器专用设置应写入未跟踪的
`CMakeUserPresets.json`，不要写入公共 CMake 文件。

## TWSE Dump 转换

`twse_dump_converter` 将 TWSE BCD dump 同时转成 Orion 兼容的 23 列 depth
CSV 和 30 列 format1 basic-info CSV。当前支持 format 1、6、17、22、23，
以及 `stock`、`etf`、`warrant`、`odd_lot`、`all` 五种 filter mode：

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /path/to/twse_stock_20260707.dump \
  --output /path/to/twse_stock_20260707.csv \
  --basic-output /path/to/twse_basic_info_20260707.csv \
  --trading-day 20260707 \
  --symbol-filter-mode stock
```

只解码和校验、不生成 CSV：

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /path/to/twse_stock_20260707.dump \
  --trading-day 20260707 \
  --symbol-filter-mode stock \
  --dry-run
```

非 dry-run 必须同时指定两个输出。转换默认拒绝覆盖已有文件；明确传入
`--overwrite` 才允许在两份临时文件均成功后替换旧输出。
schema 与时间语义见 `data/docs/data.md`，协议核对和 Orion 差异见
`data/converter/docs/twse.md`，focused test 入口见
`data/converter/docs/testing.md`。

## TAIFEX Futures Dump 转换

`taifex_dump_converter` 同步解析 TAIFEX futures I010/I011 与
I024/I025/I081/I083/I084，把全部 outright 和 calendar spread 输出为 44 列
depth CSV 与 27 列 basic-info CSV：

```bash
./build/release/data/converter/taifex_dump_converter \
  --dump /path/to/taifex_20260707.dump \
  --trading-day 20260707 \
  --output /data/tw/csv/future/taifex_20260707_future.csv \
  --basic-output /data/tw/csv/future/taifex_20260707_future_basic_info.csv
```

只校验不输出时使用 `--dry-run`。默认拒绝覆盖，明确传入 `--overwrite` 才会在
完整成功后替换已有的两份 CSV。schema、multiplier/value 口径、sequence recovery、
已知边界和 Orion 差异见 `data/converter/docs/taifex.md`。当前正式 contract 仅支持
统一日盘窗口，reader 在第一条 `13:46:00` 消息处停止；夜盘暂不转换，也不得进入
正式研究数据集。
