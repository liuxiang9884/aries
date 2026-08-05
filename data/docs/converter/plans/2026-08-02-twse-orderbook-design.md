# TWSE / TPEx 股票 Orderbook 字段设计文档计划

## 目标

- 将已完成的 TWSE / TPEx `Orderbook<N>` 字段审查写成模块专题文档。
- 明确当前 Orion-compatible struct / 23 列 CSV 与后续正式研究 schema 的边界。
- 逐一记录当前字段的保留、改名、移出和语义收紧建议。
- 记录文档中存在但当前输出缺失的状态、盘口和逐笔信息，以及不应加入
  Orderbook 的可推导字段。
- 明确 aggressor side 不是 exchange fact，并记录基于撮合前完整五档的研究推断边界。
- 记录 trade flow 与 book notional flow 的候选定义，并明确暂缓到 source schema 完成后。
- 使用固定序号的临时字段决策表完成逐项讨论，锁定后合并回唯一专题事实源并删除临时表。
- 更新 converter onboarding 和 TWSE 专题文档入口，确保新对话能找到该设计。

## 非目标

- 不修改 decoder、builder、record、CSV writer 或批量重建行为。
- 不改变当前 23 列 orderbook 与 30 列 basic-info contract。
- 不决定 TAIFEX `Orderbook<N>` 的最终字段；股票和期货保持各自 exchange
  namespace 下的独立 struct。
- 不实现或选择 money-flow factor、窗口、标准化、level weighting 或组合权重。
- 不提交或迁移旧 worktree 中尚未合并的统一 Orderbook 方案。

## 文档事实源

- `data/converter/twse/orderbook.h`
- `data/converter/twse/message_decoder.cpp`
- `data/docs/converter/twse.md`
- TWSE / TPEx 两份交易所行情传输 PDF
- `/home/liuxiang/dev/orion` 的 `StockDepth<5>`、TWSE converter、data engine 和
  `FactorCsvWriter`
- 2026-07-07 stock dump 的既有格式分布与完整转换验证

## 执行步骤

1. 建立 `twse_orderbook.md`，标记为“设计建议、尚未实现”。
2. 逐一审查当前 18 组逻辑字段，记录目标名称、单位、来源和去向。
3. 定义建议新增的 identity、source、event、OHLC 和 typed status 字段。
4. 明确 static reference、逐笔 Trade、状态事件、质量日志和可推导因子的分表边界。
5. 记录 current decoder 的 trade-only、trial、format 22 multiplier 和 CSV volume
   缺口，但不在本任务修复。
6. 更新 onboarding 与 `twse.md` 的按需阅读入口。
7. 补充成交方向的 `buy/sell/unknown`、match group、可用时点和 look-ahead 边界。
8. 记录 signed trade value、direction coverage 与五档净订单名义流，标记为 deferred。
9. 运行文档一致性搜索、`git diff --check` 和完整 diff review 后原子提交。
10. 使用临时字段决策表的 1–28 固定编号逐项锁定决定；完成后把最终 contract 合并回
    `twse_orderbook.md` 并删除临时表。

## 验证门

- 文档必须同时列出当前字段和建议最终字段，不把未来设计写成当前行为。
- 数量单位必须区分 format 6/17 的交易单位与 format 23 的股数。
- `reference_price`、涨跌停、multiplier 与动态盘口字段的归属不得互相矛盾。
- actual/trial、trade-only/full-book 和真实/fallback local time 必须有明确语义。
- aggressor side 推断必须要求连续撮合、完整 pre-match book 和无 gap；无法唯一判断时输出
  `unknown`，并记录方向实际可用的 source sequence。
- money flow 必须区分 actual executed notional 与非成交 book pressure；未知方向、第五档
  边界、市价单、currency 和可用时点均有显式规则。
- `data/docs/converter/onboarding.md` 与 `data/docs/converter/twse.md` 能定位到唯一专题事实源。
- `git diff --check` 通过，工作区不含代码或无关文件修改。

## 风险与回滚

- 该文档描述未来 schema，若措辞不清可能被误认为当前 CSV 已实现；通过显式状态、
  current/proposed 对照和 onboarding 边界避免。
- 后续实施属于独立 L3 数据 contract 迁移，必须建立新 schema/version、真实数据回归和
  历史重建计划，不能直接覆盖当前文件。
- 本任务仅修改 markdown，回滚对应文档提交即可。
