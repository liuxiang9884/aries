# Synology NAS 数据下载脚本使用说明

本文说明如何在 Ubuntu 24.04 服务器上，通过 Synology File Station API 将 NAS 中的台湾期货和股票逐笔数据增量下载到本机。

## 1. 重要行为

- 用户侧只需要使用一个入口脚本：`pull-tw-raw`。
- 当前支持 `future` 和 `stock` 两类数据，后续可在同一脚本中继续扩展 `etf` 等类型。
- 下载脚本是“按启动时的远端文件清单执行一次增量同步”，不是持续监控服务。
- 所有模式下，只要本地目标文件存在且大小与 NAS 远端文件一致，就认为该文件已经下载完成并显示 `SKIP`；不再要求修改时间完全一致。
- 未完成的文件使用 `.part` 后缀；重新运行时会尝试从断点继续下载。
- `.part` 已达到远端文件大小时直接原子发布；symlink 或其他非普通
  `.part` 会被拒绝。
- 网络失败后，包装脚本默认等待 300 秒并无限重试。
- 同一数据类型同一时间只能运行一个任务；文件锁会阻止重复启动。
- 一次同步完成后脚本会退出。NAS 后续新增或更新的每日文件需要再次启动同步。

## 2. 服务器与目录

登录数据服务器：

```bash
ssh <user>@<server>
```

相关路径：

| 用途 | 路径 |
|---|---|
| 统一下载脚本 | `/home/liuxiang/dev/aries/scripts/pull-tw-raw` |
| 通用 Synology 下载程序 | `/home/liuxiang/dev/aries/scripts/synology-pull` |
| NAS 密码文件 | `~/.config/synology-pull/password` |
| 状态与日志目录 | `~/.local/state/synology-pull/` |
| 本地数据根目录 | `/data/tw/raw/` |
| 期货本地数据 | `/data/tw/raw/future/` |
| 股票本地数据 | `/data/tw/raw/stock/` |

NAS 来源目录：

| 数据类型 | NAS 目录 | 本地目录 |
|---|---|---|
| `future` | `/taiwan_stock/FubunData/future_tick_raw` | `/data/tw/raw/future` |
| `stock` | `/taiwan_stock/FubunData/stock_tick_raw` | `/data/tw/raw/stock` |

`pull-tw-raw` 会在启动时自动创建对应的本地目录，例如 `/data/tw/raw/future` 或 `/data/tw/raw/stock`。

## 3. 启动前检查

NAS 地址和账号不写入公开仓库，需要由运行环境提供：

```bash
export SYNOLOGY_URL='https://<nas-host>:5001'
export SYNOLOGY_USER='<dsm-user>'
```

`pull-tw-raw` 要求这两个变量均已设置；`synology-pull` 的 `--url` 可替代
`SYNOLOGY_URL`，`--user` 可替代 `SYNOLOGY_USER`。

确认程序存在：

```bash
ls -l \
  /home/liuxiang/dev/aries/scripts/pull-tw-raw \
  /home/liuxiang/dev/aries/scripts/synology-pull
```

如果直接在仓库中运行，`pull-tw-raw` 会优先使用同目录下的 `synology-pull`。也可以通过 `SYNOLOGY_PULL_BIN` 指定下载程序路径：

```bash
SYNOLOGY_PULL_BIN=/path/to/synology-pull \
  /home/liuxiang/dev/aries/scripts/pull-tw-raw --type future --today
```

密码文件必须存在，并且权限应为 `600` 或更严格：

```bash
stat -c '%a %n' ~/.config/synology-pull/password
```

如需修复权限：

```bash
chmod 600 ~/.config/synology-pull/password
```

不要把密码直接写在命令行、脚本或日志中。

检查磁盘空间：

```bash
df -h /data/tw/raw
```

脚本默认要求目标磁盘至少保留 10 GiB 可用空间，可用 `MIN_FREE_BYTES` 覆盖。

## 4. 查看 NAS 目录

列出期货文件：

```bash
/home/liuxiang/dev/aries/scripts/synology-pull \
  --timeout 600 \
  --user "$SYNOLOGY_USER" \
  --password-file ~/.config/synology-pull/password \
  list /taiwan_stock/FubunData/future_tick_raw
```

列出股票文件：

```bash
/home/liuxiang/dev/aries/scripts/synology-pull \
  --timeout 600 \
  --user "$SYNOLOGY_USER" \
  --password-file ~/.config/synology-pull/password \
  list /taiwan_stock/FubunData/stock_tick_raw
```

只查看最后几项：

```bash
/home/liuxiang/dev/aries/scripts/synology-pull \
  --timeout 600 \
  --user "$SYNOLOGY_USER" \
  --password-file ~/.config/synology-pull/password \
  list /taiwan_stock/FubunData/future_tick_raw | tail
```

## 5. 下载模式

全量下载期货：

```bash
/home/liuxiang/dev/aries/scripts/pull-tw-raw --type future
```

全量下载股票：

```bash
/home/liuxiang/dev/aries/scripts/pull-tw-raw --type stock
```

下载指定日期范围，日期为闭区间：

```bash
/home/liuxiang/dev/aries/scripts/pull-tw-raw \
  --type future --start-date 20260701 --end-date 20260731
/home/liuxiang/dev/aries/scripts/pull-tw-raw \
  --type stock --start-date 20260701 --end-date 20260731
```

只下载某个开始日期之后：

```bash
/home/liuxiang/dev/aries/scripts/pull-tw-raw \
  --type future --start-date 20260701
```

只下载某个结束日期之前：

```bash
/home/liuxiang/dev/aries/scripts/pull-tw-raw \
  --type stock --end-date 20260731
```

只下载当日文件：

```bash
/home/liuxiang/dev/aries/scripts/pull-tw-raw --type future --today
/home/liuxiang/dev/aries/scripts/pull-tw-raw --type stock --today
```

`--today` 默认使用 `Asia/Taipei` 日期。需要覆盖时：

```bash
TODAY_TZ=Etc/UTC \
  /home/liuxiang/dev/aries/scripts/pull-tw-raw --type future --today
```

## 6. 后台启动下载

先创建日志目录：

```bash
mkdir -p ~/.local/state/synology-pull
```

后台启动期货当日下载：

```bash
setsid -f /home/liuxiang/dev/aries/scripts/pull-tw-raw \
  --type future --today \
  >> ~/.local/state/synology-pull/future_raw.log 2>&1
```

后台启动股票当日下载：

```bash
setsid -f /home/liuxiang/dev/aries/scripts/pull-tw-raw \
  --type stock --today \
  >> ~/.local/state/synology-pull/stock_raw.log 2>&1
```

`setsid -f` 会让任务脱离当前 SSH 会话；关闭终端不会中断下载。

如果相同类型任务已在运行，新启动的脚本会输出 `another ... download is already running` 并退出，不会产生两个并发下载进程。

## 7. 查看运行状态

查看下载进程：

```bash
pgrep -af 'synology-pull.*(future_tick_raw|stock_tick_raw)'
pgrep -af 'pull-tw-raw'
```

实时查看期货日志：

```bash
tail -f ~/.local/state/synology-pull/future_raw.log
```

实时查看股票日志：

```bash
tail -f ~/.local/state/synology-pull/stock_raw.log
```

日志常见标记：

| 标记 | 含义 |
|---|---|
| `GET` | 开始下载该文件 |
| `OK` | 文件已完整下载并校验大小 |
| `SKIP` | 本地已有相同大小的文件，跳过 |
| `WARN` | 本次请求失败，脚本将自动重试 |
| `INFO completed` | 本次启动时取得的远端清单已经同步完成 |

## 8. 统计下载量

查看两个目录占用空间：

```bash
du -sh \
  /data/tw/raw/future \
  /data/tw/raw/stock
```

统计期货完整文件和临时文件：

```bash
find /data/tw/raw/future \
  -maxdepth 1 -type f -name '*.dump.tar.gz' | wc -l

find /data/tw/raw/future \
  -maxdepth 1 -type f -name '*.part' -printf '%f %s bytes\n'
```

统计股票完整文件和临时文件：

```bash
find /data/tw/raw/stock \
  -maxdepth 1 -type f -name '*.dump.tar.gz' | wc -l

find /data/tw/raw/stock \
  -maxdepth 1 -type f -name '*.part' -printf '%f %s bytes\n'
```

查看精确字节数：

```bash
du -sb \
  /data/tw/raw/future \
  /data/tw/raw/stock
```

## 9. 停止任务

任务启动后，脚本的 PID 会写入状态目录。停止时应终止整个进程组，使包装脚本及其当前下载子进程一起退出。

停止期货任务：

```bash
pid=$(cat ~/.local/state/synology-pull/future_raw.pid)
pgid=$(ps -o pgid= -p "$pid" | tr -d ' ')
[ -n "$pgid" ] && kill -TERM -- "-$pgid"
```

停止股票任务：

```bash
pid=$(cat ~/.local/state/synology-pull/stock_raw.pid)
pgid=$(ps -o pgid= -p "$pid" | tr -d ' ')
[ -n "$pgid" ] && kill -TERM -- "-$pgid"
```

随后检查进程是否已经退出：

```bash
pgrep -af 'synology-pull.*(future_tick_raw|stock_tick_raw)'
pgrep -af 'pull-tw-raw'
```

不要直接删除 `.part` 文件；保留它才能在下次启动时断点续传。

## 10. 重新启动和每日增量

任务中断或 NAS 新增 / 更新当日文件后，直接再次执行对应命令即可。

重新运行时：

1. 本地已有且大小与远端一致的文件会被跳过。
2. `.part` 文件会尝试断点续传。
3. 缺失的新文件会继续下载。

建议每个交易日的数据在 NAS 生成后运行：

```bash
/home/liuxiang/dev/aries/scripts/pull-tw-raw --type future --today
/home/liuxiang/dev/aries/scripts/pull-tw-raw --type stock --today
```

脚本本身不会自动定时启动；当前数据服务器的 `cron` 配置和日志路径见
`docs/data.md`。其他环境如需无人值守同步，应另外配置 `systemd timer` 或
`cron`。

## 11. 下载单个文件

下载指定的期货文件：

```bash
/home/liuxiang/dev/aries/scripts/synology-pull \
  --timeout 600 \
  --user "$SYNOLOGY_USER" \
  --password-file ~/.config/synology-pull/password \
  pull \
  /taiwan_stock/FubunData/future_tick_raw/taifex_20260730.dump.tar.gz \
  /data/tw/raw/future/taifex_20260730.dump.tar.gz
```

下载指定的股票文件时，将远端和本地文件名替换为 `twse_stock_YYYYMMDD.dump.tar.gz`，本地目录替换为 `/data/tw/raw/stock/`。
