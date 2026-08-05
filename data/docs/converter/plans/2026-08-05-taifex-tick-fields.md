# TAIFEX 逐笔行情字段审查计划

日期：2026-08-05

状态：已完成

## 目标

以 TAIFEX《期貨逐筆行情資訊傳輸作業手冊 V1.5.1》为协议事实源，结合 Orion
TAIFEX data engine/data reader/orderbook builder、Aries 当前 converter，以及
2026-08-03 真实 dump，形成一份可以逐字段讨论的期货逐笔行情 schema 建议。

文档应像 `twse_orderbook.md` 一样明确每个字段的来源、类型、单位、是否进入 C++
record、是否输出 CSV、建议动作和理由，并将 Orderbook、Trade、逐笔委托统计、衍生盘口、
状态及 session statistics 分类列出。

## 非目标

- 本轮不修改 converter、CSV schema 或历史数据。
- 本轮不实现夜盘交易日映射。
- 不从五档变化伪造逐笔委托、撤单、aggressor side 或 exchange 未提供的 order id。
- 不把收盘统计或 snapshot 恢复数据误写成实时逐笔事件。

## 关键边界

- 交易所 wire field、converter normalized field、研究层派生字段必须分开标注。
- `MATCH-TIME`、`SHOW-TIME`、`INFORMATION-TIME` 与 offline fallback `local_ns`
  不得混用。
- actual/trial、普通五档/derived book、实时消息/recovery snapshot 必须保持独立状态。
- 每项建议都要注明当前 Aries 是否已实现；建议 schema 不能写成当前事实。
- 真实数据结论绑定 2026-08-03 日盘 dump；必要时用 2026-07-07 样本复核覆盖率。

## 工作项

1. 将官方 PDF 转为临时文本，核对 I010/I011/I012、I024/I025/I030、I070-I073、
   I081/I083/I084/I140 的完整字段和协议语义。
2. 逐字段核对 Orion 与 Aries 的 struct、decoder、builder 和 CSV writer。
3. 对 2026-08-03 raw/CSV 做消息类型、时间、multi-item/continuation、trial、derived、
   gap/recovery 和特殊价格的可复现实证统计。
4. 重构 `taifex_orderbook_trade.md`，按 record 分类给出编号字段审查表和建议 C++ schema。
5. 更新 converter onboarding，使新文档成为 TAIFEX 下一版字段设计的唯一入口。
6. 独立复核协议字段覆盖、实现事实、真实数据数字和文档内部一致性。

## 验证策略

- PDF 字段表与文档建议逐类对照，不遗漏协议中的可研究字段。
- 所有真实数据数字保留命令、样本日期、输入 hash 或既有验证产物位置。
- 检查每个建议输出字段均有 source、timestamp、unit、trial/recovery 语义。
- 检查所有“不输出”字段都有明确归属：日志、manifest、basic-info、session statistics
  或研究派生层。
- 运行 `git diff --check`，检查 Markdown 表格列数与内部链接，审阅完整 diff。

## 回滚

本轮只修改设计文档和 onboarding。回滚该独立提交即可恢复，不影响 converter、既有 CSV
或真实数据。

## 未决风险

- V1.5.1 PDF 与 2026 实盘存在已知 version/body 扩展；无法由官方文档确认的扩展字节
  只能列为未知，不推测语义。
- 交易所不提供逐笔委托 id，也不直接提供每笔成交 aggressor side；只能明确可推导边界，
  不能把推断字段写成 source fact。
- I084 可恢复 book 与部分累计统计，但无法恢复遗漏 I024 的逐项成交价和精确 value。

## 完成结果

- `taifex_orderbook_trade.md` 已重构为逐字段设计事实源，按 P0/P1/P2 分类列出
  `Orderbook<N>`、`Trade`、`TradePacket`、`DerivedOrderbook<1>`、
  `OrderbookDelta`、`HighLow`、`OrderStats`、`QuoteRequest`、underlying、I140 status、
  I070-I073 session statistics 与 I084 recovery 字段归属。
- 官方字段已与 V1.5.1 逐项复核，明确 `MATCH-TIME`、`SHOW-TIME`、
  `INFORMATION-TIME`、per-product sequence、first/continuation、actual/trial、market
  sentinel、signed/zero spread 与 recovery snapshot 语义。
- 2026-08-03 全文件 scanner 绑定 5,423,972,003-byte dump 及 SHA-256，覆盖消息类型、
  multi-item、时间差、I081 action、I083 trial/empty/sentinel、I030/I100/I140/I084 与盘后
  I070-I073 数量。
- Orion/Aries/DIFH6 对照结论已写入设计：新 schema 不兼容旧宽表，Trade 与 book 独立，
  不复制 Orion multi-item volume、sticky trade/action 或误命名 count 字段。
- 本轮只修改设计文档与 converter onboarding，没有修改 converter、CSV 或历史数据。
