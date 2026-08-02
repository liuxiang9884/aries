# TAIFEX Orderbook / Trade 字段设计计划

日期：2026-08-02

状态：已完成

## 目标

在不修改当前 converter 行为的前提下，形成 TAIFEX futures 专属的数据 contract
建议：逐一审查当前 `Orderbook<N>` 字段，并为 I024 设计能够无损表达多成交项、
continuation、actual/trial 和两类交易所时间的 `Trade`。

## 范围

- TAIFEX futures 日盘 dump；夜盘继续 deferred。
- 普通五档 Orderbook、I024 Trade，以及与主表直接相关的 derived book、delta、
  trade/order statistics 分表边界。
- 交易所 V1.5.1 手册、Orion TAIFEX data engine/data reader/orderbook builder/to-csv
  writer，以及 Aries 当前 44 列 converter 的差异。
- 文档与 onboarding；本计划不实施 schema、不重建 CSV。

## 工作项

1. 核对 I024、I025、I081、I083、I084 的字段、时间与 product sequence contract。
2. 核对 I030、I070-I073，确认累计委托、未平仓量和收盘统计不属于 Orderbook。
3. 逐字段审查当前 Aries `Orderbook<5>` 与 44 列 CSV。
4. 明确普通 actual book、trial book 与 derived book 的独立状态边界。
5. 设计一条 I024 price/quantity item 对应一行的扁平 `Trade`，保留 packet
   provenance，但不伪造 aggressor side 或 incoming-order id。
6. 记录当前 Aries 与 Orion 的时间、trial、open、action、derived、gap/value 等问题。
7. 更新 TAIFEX 专题入口与 converter onboarding。

## 关键决策

- 股票与期货继续使用独立 exchange-specific record，不建立字段并集或公共数据基类。
- TAIFEX 主 `Orderbook<N>` 只表示普通 N 档；衍生一档进入
  `DerivedOrderbook<1>`。
- I081 的逐 entry action 若需要持久化，进入 `OrderbookDelta`，不压缩成主表布尔值。
- I024 每个 price/quantity item 都生成一行 `Trade`；`MATCH-TIME` 是业务事件时间，
  header `INFORMATION-TIME` 单独保留。
- I024 packet 的 first/continuation bit、`PROD-MSG-SEQ` 和 item index 原样保留；
  不把推导出的 incoming-order group id 当作交易所事实。
- I024 买卖成交累计笔数进入 packet-level `TradeStats`；I030 进入 `OrderStats`；
  I072 的 open interest 进入 session/daily statistics。
- 当前正式 44/27 列 CSV 不因本文变化；未来实施必须创建新的 schema/version 和
  generation manifest。

## 验证门

- 每个建议字段都有 source、时间、单位、actual/trial 与 gap 语义。
- I024 multi-item 与 continuation 不丢价格路径，也不把 packet summary 错写成每条
  item 的 exchange fact。
- I083 trial snapshot 不覆盖 actual book；试撮市价 sentinel 与合法零价 spread
  可以无歧义区分。
- I081 普通与 derived 更新均不丢失，且只按 source entry 顺序应用。
- Orion 与 Aries 的差异逐项记录，文档不把建议 schema 写成当前已实现行为。
- `git diff --check` 通过，文档变更作为独立原子提交。
