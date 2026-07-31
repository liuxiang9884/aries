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

`twse_dump_converter` 将 TWSE BCD dump 转成 Orion 兼容的 23 列 legacy
CSV。当前支持 format 1、6、17、22、23，以及 `stock`、`etf`、
`warrant`、`odd_lot`、`all` 五种 filter mode：

```bash
./build/release/data/converter/twse_dump_converter \
  --dump /path/to/twse_stock_20260707.dump \
  --output /path/to/twse_stock_20260707.csv \
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

转换默认拒绝覆盖已有文件；明确传入 `--overwrite` 才允许成功后原子替换。
schema 与时间语义见 `docs/data.md`，协议核对和 Orion 差异见
`docs/twse_converter.md`，测试入口见 `docs/testing.md`。TAIFEX converter
尚未提取。
