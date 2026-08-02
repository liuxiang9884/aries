# Data Converter 模块 Onboarding

更新时间：2026-08-02

## 模块职责

负责将交易所原始 dump 确定性地转换为可校验的数据文件，并维护协议、时间、单位、
输出 schema 和兼容性边界。

## 当前状态

- `twse_dump_converter` 已支持 TWSE listed 与 TPEx / OTC 的 format 1、6、17、
  22、23，以及五种 Orion-compatible filter mode。
- converter 同时发布 Orion-compatible 23 列 orderbook CSV 与 30 列 format1
  basic-info CSV；后者按 `(trading_day, market, symbol)` 去重并排序。
- orderbook schema 保持 Orion-compatible 23 列，但 `total_value` 已纳入 format1
  `multiplier`，因此不再与 Orion legacy CSV byte-compatible。2026-07-07
  basic-info 历史基线为 40,841 行。
- TWSE converter 对 format1 cycle mismatch、局部 frame 损坏和 missing multiplier
  采用可审计的 best-effort 策略；orderbook/basic-info 成对发布，状态区分
  `published_complete` 与 `published_partial`。
- `taifex_dump_converter` 已同步解析全部 TAIFEX futures outright 与 spread，
  输出 44 列 research orderbook CSV 和 27 列 basic-info CSV；reader 在第一条
  `13:46:00` 消息处硬停止，不依赖 SHM。
- TWSE / TPEx 与 TAIFEX CSV 均使用 Nova frontend options 的
  `quill::CsvWriter`；compile-time schema 负责 header/format，完成 blocking flush
  并关闭 writer 后才发布同目录 partial。原有 `ofstream + fmt` writer 已删除。
- 2026-07-07 真实 TAIFEX dump 已按当前 contract 完成 full dry-run；读取
  61,605,862 条日盘消息，模拟输出 54,086,067 条 orderbook 行与 4,839 条 basic-info 行，
  并解析 154,880 条 I012。

## 关键入口

- 实现：`data/converter/twse/`
- TAIFEX 实现：`data/converter/taifex/`
- TWSE 批量 runner：`data/converter/scripts/rebuild_twse_csv`
- 测试：`tests/data/converter/twse/`
- TAIFEX 测试：`tests/data/converter/taifex/`
- 协议与 Orion 差异：`data/converter/docs/twse.md`
- TAIFEX schema、恢复语义与 Orion 差异：`data/converter/docs/taifex.md`
- 验证命令：`data/converter/docs/testing.md`

## 重要边界 / Contract

- converter 的归一化盘口对象统一命名为各 exchange namespace 下的
  `Orderbook<N>`，`N` 决定 bid/ask 档位数组长度；当前 TWSE 与 TAIFEX 均显式使用
  `Orderbook<5>`。交易所原始协议中的 `Depth` 名称保持不变。
- 本次 writer 迁移不改变两套 `Orderbook<5>`、basic-info record、列顺序、字段语义或
  数值格式；统一 Orderbook/Trade schema 仍是后续独立迁移。
- 当前 23 列 CSV 是 Orion legacy compatibility contract，不直接升级为正式研究 schema。
- basic-info CSV 的主键、列顺序、空值、单位和 format1 重复处理以
  `data/converter/docs/twse.md` 为事实源；Big5/CP950 名称不解析、不输出。
- 交易日按 UTC+8 自然日零点解释；offline `localtime = exchtime`、
  `symbol_id = -1`。
- legacy CSV 丢弃 bid / ask volume；正式 schema 必须另行设计和版本化。
- 非 dry-run 必须提供 orderbook 与 basic-info 两个不同路径；运行时错误不发布半文件，
  `--overwrite` 下会恢复旧输出。进程崩溃或断电跨两次 rename 不具备文件系统事务保证。
- 非 dry-run writer 要求调用方已初始化 Nova logging backend；两个 CLI 和测试入口均负责
  该生命周期。Quill 的异步 backend 错误不会由 `append_row()` 同步抛回 core，必须结合
  Nova/Quill 日志和最终文件校验判断磁盘层异常。
- TWSE frame recovery 最多向前扫描 1 MiB，只接受完整通过 frame 校验的重同步点；
  可识别的受损 symbol 后续不再输出，避免错误延续累计 `total_value`。具体状态和日志
  contract 以 `data/converter/docs/twse.md` 为准。
- 所有有意偏离 Orion 的行为必须记录在对应 exchange 专题文档并有 focused test。
- TAIFEX 的 `total_value = abs(price) * contracts * multiplier`，累计值非负。
- TAIFEX metadata/gap/cache 问题不阻止其余合约发布，必须从每日日志审查 symbol、
  消息类型、sequence 和恢复状态；当前 orderbook CSV 不含逐行质量 flag。
- 当前 TAIFEX 正式 contract 仅支持统一日盘窗口：保留盘前消息及
  `13:45:59.999999` 以前的消息，第一条 `13:46:00` 或更晚的 frame 及后续 bytes
  全部不处理。该 cutoff 也排除 group 5/6/9 在此后的消息。
- 夜盘支持 deferred；启用前必须建立交易日历映射并补真实数据回归。

## 验证命令

```bash
VCPKG_ROOT=/home/liuxiang/vcpkg \
  cmake --build --preset debug --target twse_converter_tests taifex_converter_tests
ctest --test-dir build/debug --output-on-failure
git diff --check
```

## 当前主线

TWSE / TPEx 和 TAIFEX converter 均已完成主体实现与真实 dump 回归；`/tw_backup`
的 stock/future 全量重建由后台任务逐日执行并保留解压 dump。TWSE 旧 runner 的失败
日期正使用 best-effort runner 重跑。

## 下一步

检查 `/home/liuxiang/tmp` 下本轮 stock/future runner 的 summary 与逐日日志，汇总失败
日期和受影响 symbol；用日盘硬截止后的 converter 重跑 future，夜盘支持明确 deferred。

## 按需阅读

- TWSE / TPEx 协议与已知边界：`data/converter/docs/twse.md`
- TAIFEX 协议、schema 与已知边界：`data/converter/docs/taifex.md`
- focused 与完整 dump 验证：`data/converter/docs/testing.md`
- 数据路径与发布结果：`data/docs/data.md`
- 已完成计划：`data/converter/docs/plans/`
