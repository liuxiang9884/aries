# Aries 项目接手指南

更新时间：2026-07-31T17:51:13+08:00

## 当前仓库状态

- 仓库路径：`/home/liuxiang/dev/aries`。
- 当前已提交的顶层模块目录：`data/`、`factors/`、`models/`、`backtest/`、`research/`、`configs/`、`tests/`、`docs/`。
- 根 `CMakeLists.txt`、`CMakePresets.json` 和 `vcpkg.json` 提供统一的
  configure、build、CTest 和依赖入口；机器专用路径不写入公共 CMake。
- `data/converter/twse/` 已包含 BCD、message decoder、state、legacy CSV
  writer、dump framing 和 `twse_dump_converter` CLI；头文件与实现文件在
  同一目录。`data/converter/taifex/` 仍是待提取骨架。
- TWSE converter 同时支持 `service_type = 01` 的上市股票和
  `service_type = 02` 的 TPEx / OTC 股票；协议核对、兼容语义及所有有意的
  Orion 差异集中记录在 `docs/twse_converter.md`。
- `README.md` 记录当前构建入口和本地 Nova source override 用法。
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

第一层目录按业务模块划分，不按 Python / C++ 语言划分。台湾 dump converter
统一放在 `data/converter/`，TWSE 和 TAIFEX 按市场划分子目录。

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
- Aries TWSE converter 对完整 2026-07-07 stock dump 读取 25,993,761 条
  message，生成 15,886,026 条数据行；输出 bytes、SHA-256 与 Orion CSV
  完全相同，`cmp` 返回 0。验证时已启用 service、format version、XOR
  checksum、terminal 和动态长度校验。

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
VCPKG_ROOT=/home/liuxiang/vcpkg \
  cmake --preset debug \
  -DFETCHCONTENT_SOURCE_DIR_NOVA=/home/liuxiang/dev/nova
VCPKG_ROOT=/home/liuxiang/vcpkg cmake --build --preset debug
VCPKG_ROOT=/home/liuxiang/vcpkg ctest --preset debug
PYTHONDONTWRITEBYTECODE=1 \
  python3 -m unittest -v tests/test_synology_pull.py
bash -n scripts/pull-tw-raw
scripts/pull-tw-raw --help
scripts/synology-pull --help
```

当前没有 Python package、回测 smoke 或模型训练入口。TWSE converter 已有
GoogleTest synthetic fixtures、malformed input tests、CLI smoke、dry-run
和完整文件兼容性证据。新增数据、因子、模型或回测逻辑时，需要同步建立对应
最小测试、fixture、dry-run 或 smoke。

## 下一步建议

- 按 `data/converter/twse/` 的同级结构提取 Orion TAIFEX dump converter，
  先锁定其 40 列 legacy schema、消息类型和时间语义，再建立小样本 fixture、
  失败边界与 2026-07-07 完整 CSV 兼容性验证。
- 明确 `/data/tw/raw`、`/home/liuxiang/data/raw` 和
  `/home/liuxiang/data/csv` 的角色、生命周期与版本规则，再建立第一版
  manifest。
- 在设计数据 schema、因子库、模型训练切分或回测口径前，优先启用 `grill-me-enhanced` 做 5 问一轮的需求追问和计划审查。
