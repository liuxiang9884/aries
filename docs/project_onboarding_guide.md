# Aries 项目接手指南

更新时间：2026-07-31T15:42:19+08:00

## 当前仓库状态

- 仓库路径：`/home/liuxiang/dev/aries`。
- 当前分支：`main`；本轮提交在本地完成，尚未 push。
- 当前已提交的顶层模块目录：`data/`、`factors/`、`models/`、`backtest/`、`research/`、`configs/`、`tests/`、`docs/`。
- `README.md` 仍是最小占位，只包含项目名。
- `scripts/` 提供台湾 raw 数据 NAS 下载入口和使用说明；
  `tests/test_synology_pull.py` 覆盖关键续传与配置边界。
- 公开仓库不保存 NAS URL、账号或密码；本机 cron 通过环境变量注入 URL 和
  账号，密码从权限受限的本地文件读取。

## 项目职责

`aries` 是日内策略研究仓库，当前规划分为四条主链路：

- 数据处理和存储：原始行情获取、解压、转换、schema、分区、质量检查和数据版本管理。
- 因子计算、验证和因子库：因子定义、批量计算、覆盖率、IC / RankIC、分组收益、decay、turnover、稳定性和样本外验证。
- 模型训练与推理：label、feature set、训练 / 验证 / 测试切分、模型评估、推理产物和模型注册。
- 回测模块：信号对齐、持仓生成、成交假设、成本模型、绩效指标和报告。

第一层目录按业务模块划分，不按 Python / C++ 语言划分。后续如需要 C++，建议放在对应模块内部的 `cpp/` 或 native 子目录中，由 Python 入口调用。

## 当前数据状态

数据细节见 `docs/data.md`。当前关键事实：

- NAS raw 下载目标根目录为 `/data/tw/raw`，其中 `future/` 和 `stock/` 已存在。
- crontab 已配置每天北京时间 15:10 和 15:30 分别下载 `future` 与 `stock` 的当日文件，日志写入 `/data/log/crontab/`。
- `/data/tw/raw` 中已有 2026-07-29、2026-07-30 和 2026-07-31 的 future /
  stock 完整压缩包；2026-07-31 两个文件均通过 `gzip -t`，没有残留
  `.part`。
- `/home/liuxiang/data/raw/future/taifex_20260707.dump` 和 `/home/liuxiang/data/raw/stock/twse_stock_20260707.dump` 已存在。
- 2026-07-07 future / stock dump 已使用 `orion` 提交 `4282286` 的
  `build/debug` 工具转换并发布到：
  - `/home/liuxiang/data/csv/future/taifex_20260707_stock_future.csv`
  - `/home/liuxiang/data/csv/stock/twse_stock_20260707.csv`
- 输出 bytes、数据行、列数、时间范围、SHA-256、实际 config 和转换命令见
  `docs/data.md`。这些 CSV 尚未纳入正式版本化数据 contract。

## 外部依赖状态

- `/home/liuxiang/vcpkg` 已安装，并符合 `orion` 默认期望路径。
- `cpp-yyjson` 已通过 `nova` 的 vcpkg overlay port 安装到本地 vcpkg。
- `/home/liuxiang/dev/nova` 的 `main` 已包含提交 `aa1b9f5 Add cpp-yyjson vcpkg overlay port`，并已推送；临时 worktree 和功能分支已删除。
- `/home/liuxiang/dev/orion` 的 `main` 当前为 `4282286 Merge pull request #66 from dcfintech/validation/future-spot-arbitrage-native-x86`；已验证 fresh configure 和 `./build.sh -n 8 debug` 不再卡在 `cpp-yyjson` FetchContent。

## 验证入口

基础检查见 `docs/testing.md`。当前仓库至少使用：

```bash
git status --short --branch
git log --oneline -8
git diff --check
PYTHONDONTWRITEBYTECODE=1 \
  python3 -m unittest -v tests/test_synology_pull.py
bash -n scripts/pull-tw-raw
scripts/pull-tw-raw --help
scripts/synology-pull --help
```

当前没有 Python package、CMake 工程、回测 smoke 或模型训练入口。新增数据、
因子、模型或回测逻辑时，需要同步建立对应最小测试、fixture、dry-run 或
smoke。

## 下一步建议

- 为 `data/` 模块确定第一批入口：raw 文件发现、dump 解压、dump 转 CSV、
  manifest 生成和基本质量检查；当前优先把一次性 `orion` scratch config
  收敛为可参数化、可 dry-run 的转换流程。
- 明确 `/data/tw/raw`、`/home/liuxiang/data/raw` 和
  `/home/liuxiang/data/csv` 的角色、生命周期与版本规则，再建立第一版
  manifest。
- 在设计数据 schema、因子库、模型训练切分或回测口径前，优先启用 `grill-me-enhanced` 做 5 问一轮的需求追问和计划审查。
