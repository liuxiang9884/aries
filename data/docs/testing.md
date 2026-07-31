# Data 模块验证说明

更新时间：2026-07-31

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

## 定时任务与进程

```bash
crontab -l
pgrep -af 'pull-tw-raw|synology-pull'
tail -40 /data/log/crontab/future_daily.log
tail -40 /data/log/crontab/stock_daily.log
```

## Raw 文件检查

```bash
find /data/tw/raw -maxdepth 2 -type f \
  \( -name '*.dump.tar.gz' -o -name '*.part' -o -name '*.dump' \) \
  -printf '%p %s\n' | sort

gzip -t /data/tw/raw/future/taifex_YYYYMMDD.dump.tar.gz
gzip -t /data/tw/raw/stock/twse_stock_YYYYMMDD.dump.tar.gz
```
