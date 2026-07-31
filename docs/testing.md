# 验证说明

更新时间：2026-07-31T15:42:19+08:00

## 基础检查

当前仓库尚未建立 Python package、CMake 工程、通用数据校验框架、回测
smoke 或模型训练入口。`tests/test_synology_pull.py` 是当前唯一单元测试入口。
提交前至少执行：

```bash
git status --short --branch
git log --oneline -8
git diff --check
```

## 数据下载脚本检查

下载脚本修改后至少运行：

```bash
PYTHONDONTWRITEBYTECODE=1 \
  python3 -m unittest -v tests/test_synology_pull.py
bash -n scripts/pull-tw-raw
scripts/pull-tw-raw --help
scripts/synology-pull --help
git diff --check
```

当前单元测试覆盖：

- 完整 `.part` 不经网络请求直接发布。
- `.part` symlink 被拒绝且不会修改 symlink 目标。
- 未通过参数或 `SYNOLOGY_URL` 提供 NAS URL 时，CLI 拒绝执行。
- `SYNOLOGY_URL` 可作为 CLI 默认值。

查看定时任务：

```bash
crontab -l
```

查看下载进程：

```bash
pgrep -af 'pull-tw-raw|synology-pull'
```

查看 cron 日志：

```bash
tail -40 /data/log/crontab/future_daily.log
tail -40 /data/log/crontab/stock_daily.log
```

查看 raw 数据落盘状态：

```bash
find /data/tw/raw -maxdepth 2 -type f \
  \( -name '*.dump.tar.gz' -o -name '*.part' -o -name '*.dump' \) \
  -printf '%p %s\n' | sort
```

检查已完成的 gzip：

```bash
gzip -t /data/tw/raw/future/taifex_YYYYMMDD.dump.tar.gz
gzip -t /data/tw/raw/stock/twse_stock_YYYYMMDD.dump.tar.gz
```

## Dump 转 CSV 检查

一次性转换至少记录输入、工具版本、config、输出路径、bytes、数据行数、列数、
时间范围和 SHA-256。本轮 2026-07-07 的结果与边界见 `docs/data.md`。

检查文件大小与 hash：

```bash
stat -c '%n %s bytes %y' \
  /home/liuxiang/data/csv/future/taifex_20260707_stock_future.csv \
  /home/liuxiang/data/csv/stock/twse_stock_20260707.csv

sha256sum \
  /home/liuxiang/data/csv/future/taifex_20260707_stock_future.csv \
  /home/liuxiang/data/csv/stock/twse_stock_20260707.csv
```

本轮还对完整文件逐行检查：列数与 header 一致、symbol 非空、
`symbol_id = -1`、`exchtime` / `localtime` 为整数，且 `exchtime` 位于配置的
2026-07-07 本地交易日窗口。当前仓库尚未把这组检查封装成可复用命令。

## 后续测试要求

- 新增数据读取、解压、转换或质量检查逻辑时，优先提供小样本 fixture 或 dry-run。
- 新增因子逻辑时，测试应覆盖输入对齐、滞后、缺失值、排序、窗口边界和无未来数据。
- 新增模型训练逻辑时，测试或 smoke 应记录 label、feature set、时间切分、随机种子和 baseline。
- 新增回测逻辑时，测试应覆盖 signal time、order time、fill time、成本模型和不能同 bar 偷看成交的边界。
