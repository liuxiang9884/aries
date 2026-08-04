# Data Converter 模块 Onboarding

更新时间：2026-08-04

## 模块职责

负责将交易所原始 dump 确定性地转换为可校验的数据文件，并维护协议、时间、单位、
输出 schema 和兼容性边界。

## 当前状态

- `twse_dump_converter` 已支持 TWSE listed 与 TPEx / OTC 的 format 1、6、17、22、23
  及五种 Orion filter mode，并已迁移到 `twse-orderbook-v2`。
- 股票 `Orderbook<5>` 为 272-byte POD-like record；format 6/17 的同一 incoming order
  聚合成 terminal 一行，保存 `trade_side/trade_count/trade_volume`，不再生成独立 Trade。
- 股票 CSV 为 37 列，包含三个 raw state、OHLC、group trade 和全部 bid/ask volume；
  23 列 legacy writer 已删除。format1 basic-info 保持 30 列并按
  `(trading_day, market, symbol)` 去重排序。
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
- 股票 v2 已完成 focused tests 与 2026-07-07 全日真实验证；`all --dry-run` 为
  25,993,761 messages、2,312,789 actual trades、2,254,614 match groups、45,270
  multi groups、78 held-ended groups，gap/incomplete/imputation 均为 0。`stock` 完整
  CSV 为 15,548,031 行、3,705,213,634 bytes。`/tw_backup` 既有历史股票 CSV 在重建前
  仍可能是 23 列 legacy，必须按 exact header 判断。
- 期货设计明确 I024 一项一条 Trade、
  `MATCH-TIME` / `INFORMATION-TIME` 分离、actual/trial book 分离，以及普通/
  derived/statistics 分表。

## 关键入口

- 实现：`data/converter/twse/`
- TAIFEX 实现：`data/converter/taifex/`
- TWSE 批量 runner：`data/converter/scripts/rebuild_twse_csv`
- 测试：`tests/data/converter/twse/`
- TAIFEX 测试：`tests/data/converter/taifex/`
- 协议与 Orion 差异：`data/converter/docs/twse.md`
- TWSE / TPEx 股票 Orderbook 字段设计：`data/converter/docs/twse_orderbook.md`
- TAIFEX schema、恢复语义与 Orion 差异：`data/converter/docs/taifex.md`
- TAIFEX Futures Orderbook / Trade 字段设计：
  `data/converter/docs/taifex_orderbook_trade.md`
- 验证命令：`data/converter/docs/testing.md`

## 重要边界 / Contract

- converter 的归一化盘口对象统一命名为各 exchange namespace 下的
  `Orderbook<N>`，`N` 决定 bid/ask 档位数组长度；当前 TWSE 与 TAIFEX 均显式使用
  `Orderbook<5>`，但股票与期货保持独立 struct，不建立公共数据基类或字段并集。
  交易所原始协议中的 `Depth` 名称保持不变。
- 股票 v2 contract 以 `data/converter/docs/twse_orderbook.md` 为唯一事实源；期货下一版
  Orderbook / Trade 设计仍在 `data/converter/docs/taifex_orderbook_trade.md`，尚未实现。
- 股票 23 列 CSV 只是 legacy generation；代码不提供兼容 writer/reader，也不双写。
- basic-info CSV 的主键、列顺序、空值、单位和 format1 重复处理以
  `data/converter/docs/twse.md` 为事实源；Big5/CP950 名称不解析、不输出。
- 交易日按 UTC+8 自然日零点解释；offline `local_ns = exchange_ns` 并写
  `local_time_source=exchange_fallback`。股票内部使用单次运行 dense `symbol_id`，不输出
  CSV；期货仍使用当前独立 contract。
- format 6/17 trade-only 不发布；normal final 与 held terminal 各发布一条原子
  Orderbook。held 行盘口全 0，下一条完整 book 才恢复 valid。
- 股票 sequence 按 `(trading_day, market, source format)` 审计；group state 按
  `(market,symbol)` 隔离。trial 不修改 actual/pending，format23 bit 0 按 reserved 处理。
- 股票 `total_value` 在 accepted cumulative checkpoint 间先累计实际逐笔；只有
  missing volume 使用 last price。任何 gap/incomplete/imputation 都进入结构化统计和
  Nova warning，不停止其他 symbol。
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

TWSE / TPEx `twse-orderbook-v2` 实现、review、单日真实验证、完整 CTest 和
sanitizer 已在 `feature/twse-atomic-orderbook` 完成，待合并。TAIFEX 保持当前独立
schema。历史 raw dump 与 archive 均保留。

## 下一步

合并股票 v2 后，使用更新后的 runner 从 raw 重建
`/tw_backup/data/tw/csv/stock/`；summary 必须记录 schema、每日日志和 orderbook/basic
bytes，并同时核对 converter SHA-256/完成日志声明与文件 exact header；
全部日期完成后统一审查问题。
之后再恢复 TAIFEX 下一版 schema 工作。

## 按需阅读

- TWSE / TPEx 协议与已知边界：`data/converter/docs/twse.md`
- TAIFEX 协议、schema 与已知边界：`data/converter/docs/taifex.md`
- TAIFEX 下一版 Orderbook / Trade 字段建议：
  `data/converter/docs/taifex_orderbook_trade.md`
- focused 与完整 dump 验证：`data/converter/docs/testing.md`
- 数据路径与发布结果：`data/docs/data.md`
- 已完成计划：`data/converter/docs/plans/`
