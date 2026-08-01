# Data Converter 模块 Onboarding

更新时间：2026-08-01

## 模块职责

负责将交易所原始 dump 确定性地转换为可校验的数据文件，并维护协议、时间、单位、
输出 schema 和兼容性边界。

## 当前状态

- `twse_dump_converter` 已支持 TWSE listed 与 TPEx / OTC 的 format 1、6、17、
  22、23，以及五种 Orion-compatible filter mode。
- converter 同时发布 Orion-compatible 23 列 depth CSV 与 30 列 format1
  basic-info CSV；后者按 `(trading_day, market, symbol)` 去重并排序。
- 2026-07-07 `stock` depth 输出与 Orion legacy CSV bytes、行数和 SHA-256
  一致；basic-info 输出 40,841 行，SHA-256 为
  `093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a`。
- `taifex_dump_converter` 已同步解析全部 TAIFEX futures outright 与 spread，
  输出 44 列 research depth CSV 和 27 列 basic-info CSV；不依赖 SHM。
- 2026-07-07 真实 TAIFEX dump 已按当前 contract 完成 full dry-run；读取
  62,559,197 条消息，模拟输出 54,221,272 条 depth 行与 4,839 条 basic-info 行，
  并解析 181,280 条 I012。

## 关键入口

- 实现：`data/converter/twse/`
- TAIFEX 实现：`data/converter/taifex/`
- 测试：`tests/data/converter/twse/`
- TAIFEX 测试：`tests/data/converter/taifex/`
- 协议与 Orion 差异：`data/converter/docs/twse.md`
- TAIFEX schema、恢复语义与 Orion 差异：`data/converter/docs/taifex.md`
- 验证命令：`data/converter/docs/testing.md`

## 重要边界 / Contract

- 当前 23 列 CSV 是 Orion legacy compatibility contract，不直接升级为正式研究 schema。
- basic-info CSV 的主键、列顺序、空值、单位和 format1 重复处理以
  `data/converter/docs/twse.md` 为事实源；Big5/CP950 名称不解析、不输出。
- 交易日按 UTC+8 自然日零点解释；offline `localtime = exchtime`、
  `symbol_id = -1`。
- legacy CSV 丢弃 bid / ask volume；正式 schema 必须另行设计和版本化。
- 非 dry-run 必须提供 depth 与 basic-info 两个不同路径；运行时错误不发布半文件，
  `--overwrite` 下会恢复旧输出。进程崩溃或断电跨两次 rename 不具备文件系统事务保证。
- 所有有意偏离 Orion 的行为必须记录在对应 exchange 专题文档并有 focused test。
- TAIFEX 的 `total_value = abs(price) * contracts * multiplier`，累计值非负。
- TAIFEX metadata/gap/cache 问题不阻止其余合约发布，必须从每日日志审查 symbol、
  消息类型、sequence 和恢复状态；当前 depth CSV 不含逐行质量 flag。
- 当前 TAIFEX 正式 contract 仅支持日盘。夜盘暂不转换，且 CLI 不会自动拒绝夜盘；
  调用方必须保证输入只含日盘。启用夜盘前必须建立交易日历映射并补真实数据回归。

## 验证命令

```bash
VCPKG_ROOT=/home/liuxiang/vcpkg \
  cmake --build --preset debug --target twse_converter_tests taifex_converter_tests
ctest --test-dir build/debug --output-on-failure
git diff --check
```

## 当前主线

TWSE / TPEx 和 TAIFEX converter 均已完成实现与 2026-07-07 真实 dump 回归；
`/tw_backup` 的 stock/future 全量重建由后台任务逐日执行并保留解压 dump。

## 下一步

检查 `/home/liuxiang/tmp` 下本轮 stock/future runner 的 summary 与逐日日志，汇总失败
日期和受影响 symbol；只处理确认过的日盘输入，夜盘支持明确 deferred。

## 按需阅读

- TWSE / TPEx 协议与已知边界：`data/converter/docs/twse.md`
- TAIFEX 协议、schema 与已知边界：`data/converter/docs/taifex.md`
- focused 与完整 dump 验证：`data/converter/docs/testing.md`
- 数据路径与发布结果：`data/docs/data.md`
- 已完成计划：`data/converter/docs/plans/`
