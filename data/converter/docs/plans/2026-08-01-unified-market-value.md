# TWSE / TAIFEX value 统一与批量重建计划

## 目标

- 将 TWSE 与 TAIFEX 的 `total_value` 统一为包含交易单位乘数的非负累计成交额。
- 先修改并验证 TWSE converter，再从 `/tw_backup/data/tw/raw/stock/` 的已解压
  dump 优先重建 `/tw_backup/data/tw/csv/stock/` 全部可用交易日。
- 在已完成的 TAIFEX 同步 converter 上落实相同 value contract、I012 协议校验、
  日级/合约级问题摘要和可恢复错误策略，再按相同目录约定后台转换 futures。

## 非目标

- 本轮不支持 TAIFEX 夜盘日期映射。
- 本轮不输出独立逐笔成交 CSV；I024/I025 继续合并到下一条 order book 状态行。
- 不解析 TAIFEX I010 version 9 未被 V1.5.1 文档定义的末尾 8 bytes。
- 不在 converter 中硬编码近月、stock future 或 calendar spread 资产池过滤。

## 已锁定决定

- TWSE 一般交易：`total_value += delta_volume * price * multiplier`；odd-lot 的
  wire volume 已是实际证券数量，使用有效乘数 1。
- TAIFEX：`total_value += abs(price) * trade_volume * multiplier`；calendar spread
  仍表示价差名义成交额，不表示两条腿 gross notional。
- TAIFEX trial matching 保留并由 `match_flag` 标记。
- TAIFEX sequence gap、metadata 缺失和单合约状态问题不阻止当日 CSV 发布；按日期、
  symbol、消息类型、sequence 与恢复状态写入日志摘要。frame checksum、截断、非法
  BCD 和错误长度仍使当日失败，但批处理继续下一日。
- TAIFEX 不再把 gap 质量状态写入每一行 depth CSV；研究前根据日级问题摘要统一审查。
- I012 是商品多阶涨跌停价格，不是 reference price 变更。按 V1.5.1 严格解析并
  统计重复/冲突，但暂不写入固定列 CSV，也不改写 I010 `reference_price`。
- ignored 消息至少按 transmission/message kind 分项计数，不再只提供总数。

## TWSE 批量问题处理决定

以下是 2026-08-01 全量重建暴露问题后的处理 contract。当前先记录决定，待股票
问题全部讨论完成后统一实现、测试和重跑；不要把本节误读为当前 converter 已具备的
行为。

已锁定：

- format1 `AL/NE` cycle count mismatch 为非致命质量问题。记录 trading day、
  service、control kind、expected/actual 与缺失数量，状态标为
  `success_with_issues`；当天 depth CSV 和 basic-info CSV 都发布。
- basic-info CSV 只包含实际成功解析的记录，允许不完整。已有 metadata 的 symbol
  正常转换；若某 symbol 最终仍缺少计算 `total_value` 所需的 multiplier，不伪造
  multiplier/value，跳过该 symbol 的 depth 并记录 symbol 级问题。
- 无法通过 `gzip -t` 或无法解压的归档标为 `input_corrupt`，不生成或覆盖 CSV，
  保留归档与日志并加入重新下载清单；批处理继续下一日。
- 可正常解压但 member 为零字节时，先按交易日历分类：休市日标为
  `non_trading_day` 并正常结束、不生成 CSV；正常交易日标为 `empty_input`，不生成
  CSV并加入重新下载清单。

待真实数据验证后锁定：

- `20250925`、`20251014` 的 format6 frame 同时存在 terminal、checksum 和下一
  header 失步。先实现有扫描上限且要求完整 frame 校验的显式 recovery 实验模式，
  重同步到下一条可信 frame，生成 scratch depth/basic CSV；检查受影响 symbol、
  sequence、volume、`total_value`、列数和全日问题摘要后，再决定是否允许正式发布。
- 初版 runner 曾把保留的旧 CSV 当作转换成功，导致 `20250909`、`20250911` 被误标
  success；统一实现时修正 summary，并以 converter exit status 和本次 staged publish
  结果共同判定成功。

## 实施步骤

1. 为 TWSE 一般交易 multiplier 和 odd-lot 有效乘数补失败测试。
2. 最小修改 TWSE 状态与文档，完成 Debug/Release focused test 和完整 CTest 后提交。
3. 建立可重复的后台 runner：优先使用 `/tw_backup` 已解压 dump，不删除 raw，覆盖旧
   depth/basic CSV；每个日期独立执行并记录 success/failure summary。
4. 合并现有 `feature/taifex-converter`，按 PDF 核对 I012 wire layout并为 value、
   I012、gap 发布、问题摘要、ignored 分类和 44 列 schema 建立失败测试。
5. 实现、review、运行 focused/full tests，并用 2026-07-07 真实数据做 smoke/full
   验证后提交。
6. 从 `/tw_backup/data/tw/raw/future/` 优先读取已解压 dump；必要时只解压缺失日期且保留
   dump，输出到 `/tw_backup/data/tw/csv/future/`，逐日记录日志并继续后续日期。

## 验证

- `git diff --check`。
- TWSE/TAIFEX focused unit 与 dump converter tests。
- Debug/Release build 及完整 CTest。
- 用真实 CSV 抽查 `total_value` 单调非负、TWSE 量纲、TAIFEX multiplier、列数、日期、
  symbol 和问题摘要。
- 后台启动后检查 PID/service、首个交易日日志、输出 `.partial` 与最终发布行为。

## 回滚

- 代码按 TWSE、TAIFEX 分成原子提交，可分别 revert。
- CSV 使用 staged writer；单日失败保留旧文件或不发布新文件。
- 批量重建使用 `--overwrite`，原始压缩包和已解压 dump 均保留，可用旧 commit 的
  converter 再生成历史口径。

## 未决风险

- TAIFEX 夜盘仍不能按交易日正确映射事件自然日，后台任务只能选择已确认的日盘输入。
- sequence gap 后缺失成交无法从 snapshot 恢复精确 value；日志必须列出受影响 symbol。
- TWSE `total_value` contract 改变后不再与 Orion legacy CSV byte-compatible。
