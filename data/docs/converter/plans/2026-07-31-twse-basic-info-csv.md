# TWSE format1 basic-info CSV 实施计划

## 目标

在不改变既有 depth CSV 内容的前提下，扩展离线 `twse_dump_converter`：解析 TWSE/TPEX format1 商品基本资料，生成按交易日、市场、证券代码排序且去重后的 basic-info CSV；继续严格校验 dump frame，并确保 depth 与 basic-info 两个输出同时成功发布或同时保持旧版本。

## 非目标

- 本轮不实现 format22。
- 本轮不改变既有 depth CSV schema、筛选模式和数值口径。
- 本轮不解析或输出 Big5/CP950 名称、非十元面额标志。
- 本轮不决定 TWSE 与 TAIFEX 的 `volume`、`quantity`、`multiplier` 统一 contract。
- 本轮不修改实盘行情路径；严格 frame 校验只用于离线 converter。

## 已锁定 contract

- 主键为 `(trading_day, market, symbol)`；service `01` 映射为 `TWSE`，service `02` 映射为 `TPEX`。
- 同主键、所选字段完全一致的重复记录忽略并计数；同主键字段变化时立即失败，错误包含旧/新 offset、sequence 和字段差异，不采用 last-wins。
- `AL`、`NE` 是控制记录，只校验周期计数，不输出数据行。
- 所有价格在内存中使用 `double`，CSV 固定 4 位小数；`exercise_ratio` 使用每一权证口径，CSV 固定 5 位小数。
- 权证数量 wire 字段按千权证单位解码，CSV 输出实际单位（乘 1000）；wire 行使比例为每 1000 权证对应标的数量，CSV 输出每一权证比例（除 1000）。
- `currency` 空白规范化为 `TWD`；非权证不适用字段输出空值；`maturity_date` 输出 `YYYY-MM-DD`。
- `multiplier` 直接从 format1 解析；权证分类以 `warrant_flag`、`security_type`、`market_data_line` 为依据，不仅依赖证券代码格式。

basic-info CSV 列顺序如下：

```text
trading_day
market
symbol
industry_code
security_type
anomaly_code
stock_group_code
board_code
reference_price
high_limit
low_limit
abnormal_recommendation
special_abnormal
day_trading_code
margin_short_exempt
borrow_short_exempt
matching_cycle_seconds
warrant_flag
strike_price
previous_exercise_volume
previous_cancellation_volume
outstanding_volume
exercise_ratio
warrant_upper_price
warrant_lower_price
maturity_date
foreign_stock_flag
multiplier
currency
market_data_line
```

## 边界与风险

- format1 的 TWSE/TPEX body offset 不完全相同；解析测试必须分别覆盖两种 layout。
- format1 没有可用于安全确定版本先后的事件时间；发现同键字段变化时失败，以避免静默引入 look-ahead 或错误状态。
- 两个输出文件的最终 rename 不是文件系统级事务；实现需用 staging/backup/rollback 协议，发布任一步失败时恢复旧文件并清理临时文件。
- 初始实现因异步错误传播边界保留同步 writer；2026-08-02 按用户确认迁移到 Nova
  frontend 的 Quill writer，并保留 partial/rollback，异步错误传播限制由当前
  `data/docs/converter/twse.md` 明确记录。

## 实施步骤

1. 为 TWSE/TPEX format1 正常记录、控制记录、数值缩放、空值、重复和冲突补失败测试。
2. 增加 basic-info 数据模型、format1 decoder 和以主键索引的 catalog；保持 message/depth 解析接口的职责边界。
3. 修正 warrant 分类，使过滤仍保持既有语义但采用 format1 元数据。
4. 增加 basic-info CSV writer，并把 converter CLI 扩展为 `--output <depth.csv>` 与 `--basic-output <basic.csv>`。
5. 将 CLI 日志接入 Nova；core 继续通过异常返回错误，避免重复记录。
6. 实现双文件 staging、发布、覆盖和失败回滚；dry-run 不要求输出路径且不生成文件。
7. 运行 focused unit/integration tests、格式检查与 diff review，按最小闭环提交。
8. 用 20260707 完整 dump 验证：depth CSV 行数、字节数和 SHA256 必须保持既有基线，并核对 format1 统计与 basic-info 唯一行数。
9. 更新 converter onboarding、TWSE contract 和测试文档，记录与 Orion 不同的行为和验证边界。

## 验证基线

- 输入：`/home/liuxiang/data/raw/stock/twse_stock_20260707.dump`
- 既有 depth CSV：`/home/liuxiang/data/csv/stock/twse_stock_20260707.csv`
- depth rows：`15,886,026`
- depth bytes：`2,742,684,274`
- depth SHA256：`f5981991517c24d07fbe4ee2ef38d9b9d3d198b69d2c841cdab39d5a8cb3cc41`
- dump format1 normal messages：`1,145,472`
- format1 controls：`167`
- 唯一 `(market, symbol)`：`40,841`
- 完全相同的重复：`1,104,631`
- 已观测字段变化重复：`0`

## 回滚

代码按原子提交拆分，可逐项 revert。运行时输出先写临时文件；任何解析、写入、校验或发布错误都返回非零并清理临时文件，覆盖模式下恢复运行前的旧输出。
