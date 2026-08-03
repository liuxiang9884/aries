# TWSE / TPEx 股票 Orderbook 字段决策表

更新时间：2026-08-03

状态：**逐项讨论中，尚未实现**。本文是股票下一版 `Orderbook<N>` 字段去留与 CSV
持久化决策的唯一清单；字段语义、协议来源和边界仍以
`data/converter/docs/twse_orderbook.md` 为准。当前 converter、C++ record 和
Orion-compatible 23 列 CSV 均未因本文改变。

## 使用方法

- 序号固定，后续讨论直接使用序号，不因字段改名或决定变化而重排。
- “建议输出 CSV”指**下一版正式研究 Orderbook CSV**，不是当前 legacy CSV。
- “是（展开）”表示 C++ 内部保留 typed wrapper，CSV 输出稳定、可读的拆分列，不输出
  packed decimal。
- `ask_*[N]` / `bid_*[N]` 在 C++ 中是数组，在 CSV 中按 `1..N` 展开。
- “最终决定”只有用户确认后才从“待讨论”改为确定结论。
- 本表只审查 Orderbook；独立 `Trade`、basic-info、status dataset、质量日志和研究衍生
  指标不在本表增加字段。

## 字段清单

| 序号 | 字段 | 来源 / 当前情况 | 建议输出 CSV | 我的建议 | 最终决定 |
|---:|---|---|:---:|---|---|
| 1 | `symbol` | 现有 C++ 字段；当前 CSV 已输出 | 是 | **保留**。使用去除尾端空白的交易所证券代码；持久化身份与 `trading_day`、`market` 共同确定 | 待讨论 |
| 2 | `symbol_id` | 当前 C++ `Orderbook` 没有该字段；legacy CSV 固定输出 `-1` | 否 | **增加为内部字段**，建议 `int32_t`、无效值 `-1`。作为单次运行内的 dense id，加速 builder 与关联查找；不作为跨进程、跨交易日稳定标识 | **已确定：内部保留，CSV 不输出** |
| 3 | `exchange_ns`（现 `exchtime`） | 现有 C++ / CSV 字段名为 `exchtime` | 是 | **改名并收紧语义**。表示触发本行的交易所事件时间，Unix epoch ns；wire 只有 us 精度，低三位为 0 | 待讨论 |
| 4 | `local_ns`（现 `localtime`） | 现有 C++ / CSV 字段名为 `localtime`；offline 当前等于 `exchtime` | 是 | **改名并收紧语义**。正式语义是真实接收时间；offline fallback 必须在 manifest 标记，且不得用于延迟研究 | 待讨论 |
| 5 | `status` | 现有 `int64_t`，把三个协议 bitmap 打包；当前 CSV 输出十进制值 | 否 | **删除并拆分**为第 26–28 项 typed state；不再输出含义不稳定的 packed integer | 待讨论 |
| 6 | `last_price` | 现有 C++ / CSV 字段 | 是 | **保留并收紧语义**。仅表示截至本行最近一笔 actual 成交价；trial、held/status 揭示不得覆盖 | 待讨论 |
| 7 | `reference_price`（现 `previous_close`） | 现有 C++ 字段名为 `previous_close`；当前 CSV 未输出 | 否 | **改名并移至 basic-info/reference state**。今日参考价不应假定为未经调整的昨日收盘价，也不在每条 Orderbook 重复 | 待讨论 |
| 8 | `open` | 现有 C++ / CSV 字段 | 是 | **保留并收紧语义**。当日首笔 actual 成交价；trial 不更新 | 待讨论 |
| 9 | `high_limit` | 现有 C++ / CSV 字段 | 否 | **移至 basic-info/reference state**。属于日级静态价格限制，不在每条 Orderbook 重复 | 待讨论 |
| 10 | `low_limit` | 现有 C++ / CSV 字段 | 否 | **移至 basic-info/reference state**。同 `high_limit` | 待讨论 |
| 11 | `multiplier` | 现有 C++ 字段；当前 CSV 未输出；builder 计算 `total_value` 时使用 | 否 | **移出 Orderbook，保留在 basic-info 与内部 builder state**。format 6/17 用于交易单位换算；format 23 数量已是股数，成交额计算的有效 multiplier 为 1 | 待讨论 |
| 12 | `total_volume` | 现有 C++ / CSV 字段 | 是 | **保留并收紧语义**。交易所 actual 累计成交量；数量单位由 `source_message` 与 generation contract 限定 | 待讨论 |
| 13 | `total_value` | 现有 C++ / CSV 字段，由 converter 累计计算 | 是 | **保留并收紧语义**。actual 非负累计成交额，币种来自 basic-info；gap 后不能静默视为完整 | 待讨论 |
| 14 | `trade_volume`（现 `total_trade`） | 现有 C++ / CSV 字段名为 `total_trade`，实际不是累计成交笔数 | 是 | **改名并修正语义**。表示触发当前 Orderbook 行的 actual 成交量；无 actual 成交时为 0 | 待讨论 |
| 15 | `ask_price[N]` | 现有 C++ 数组；当前 CSV 输出五档价格 | 是 | **保留并按档展开**。表示最近一次完整普通盘口揭示的卖方价格；空档与市价委托须结合 volume 判断 | 待讨论 |
| 16 | `ask_volume[N]` | 现有 C++ 数组；当前 CSV 丢弃 | 是 | **保留并首次正式持久化**。交易所直接提供，不应继续丢失；数量单位遵循对应 source contract | 待讨论 |
| 17 | `bid_price[N]` | 现有 C++ 数组；当前 CSV 输出五档价格 | 是 | **保留并按档展开**。表示最近一次完整普通盘口揭示的买方价格 | 待讨论 |
| 18 | `bid_volume[N]` | 现有 C++ 数组；当前 CSV 丢弃 | 是 | **保留并首次正式持久化**。同 `ask_volume[N]` | 待讨论 |
| 19 | `source_sequence`（现 `sequence`） | 现有 C++ / CSV 字段名为 `sequence`，类型为 `int64_t` | 是 | **改名并改为 `uint64_t`**。它是 source header sequence；scope 必须由 trading day、market、format 共同限定，不作为全局行号 | 待讨论 |
| 20 | `trading_day` | 建议新增；当前只能从运行参数/文件分区获得 | 是 | **新增**。建议 `int32_t YYYYMMDD`；使 CSV 行自描述，并参与稳定主键和跨日拼接校验 | 待讨论 |
| 21 | `market` | 建议新增；decoder 已能由 service type 区分 TWSE / TPEX | 是 | **新增 typed enum 并持久化**。同一 symbol 代码需要以 market 区分，禁止仅靠输出目录推断 | 待讨论 |
| 22 | `source_message` | 建议新增；当前 CSV 不区分 format 6 / 17 / 23 | 是 | **新增 typed enum 并持久化**。限定 sequence scope、数量单位和协议语义，也是 regular/odd-lot 数据不可混用的显式依据 | 待讨论 |
| 23 | `event_type` | 建议新增；当前只能重新解释 raw status 推断 | 是 | **新增 typed enum 并持久化**。建议取值 `trade` / `orderbook` / `trade_orderbook` / `status`，明确本消息是否更新成交或普通五档 | 待讨论 |
| 24 | `high` | 建议新增；当前 builder 未保存/输出 | 是 | **新增**。截至本行的 actual 当日最高成交价；不得用日终值回填早期行 | 待讨论 |
| 25 | `low` | 建议新增；当前 builder 未保存/输出 | 是 | **新增**。截至本行的 actual 当日最低成交价；同 `high` | 待讨论 |
| 26 | `disclosure` | 建议新增 typed 1-byte wrapper；来源为 format 6/17/23 disclosure byte | 否 | **内部保留，CSV 不输出 raw wrapper**。成交 payload、揭示档数与 trade-only/full-book 语义先归一化到 `event_type`、Trade match group 和 book 更新行为；raw byte 用于 decoder 校验与诊断 | 待讨论 |
| 27 | `limit_state` | 建议新增 typed 1-byte wrapper；来源为交易所 limit byte | 是（展开） | **内部保留 typed state，CSV 展开**为 `trade_limit`、`best_bid_limit`、`best_ask_limit`、`instantaneous_trend`，避免 packed integer，同时保留涨跌停/暂缓撮合研究信息 | 待讨论 |
| 28 | `session_state` | 建议新增 typed 1-byte wrapper；来源为交易所 status byte | 是（展开） | **内部保留 typed state，CSV 展开**为 `is_trial`、`is_delayed_open`、`is_delayed_close`、`matching_method`、`is_opening`、`is_closing`；reserved bits 只校验、不持久化 | 待讨论 |

## 当前已锁定决定

1. 第 2 项 `symbol_id`：加入内部 record/state，用于运行期高效关联；不写入 CSV。
2. 同一 converter generation 内，`Orderbook`、`Trade` 与 basic-info/catalog 对同一证券使用
   相同 `symbol_id`。
3. `symbol_id` 不提供跨进程、跨交易日、跨文件稳定性；持久化 identity 仍为
   `(trading_day, market, symbol)`。

## 后续讨论结果记录规则

每确认一项，直接更新对应行的“最终决定”；如果决定引入新字段，在第 28 项之后追加新序号，
不复用或重排已有序号。全部锁定后，再从本表生成正式 C++ schema、CSV 列顺序、schema
version、fixture 与历史重建计划。
