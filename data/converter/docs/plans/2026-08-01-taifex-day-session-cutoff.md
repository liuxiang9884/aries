# TAIFEX 日盘硬截止实施计划

## 目标

`taifex_dump_converter` 只处理 `13:45:59.999999` 以前已进入 dump 的日盘消息；
盘前轮播的基本资料仍然保留。读取到第一条 `13:46:00` 或更晚的 frame header 后
立即结束转换，
不读取、不校验、也不统计其 body 及后续 bytes，避免盘后基本资料覆盖日盘状态。

## 非目标

- 不支持 TAIFEX 夜盘及其跨自然日 timestamp 映射。
- 不按不同 `FLOW-GROUP` 分别设置收盘时间。
- 不改变 depth/basic-info CSV schema、价格、volume 或 value 口径。
- 不在本项修改中处理日盘截止前已经存在的 frame 损坏。

## 关键决定与边界

- 截止采用项目既有 Orion `end_building_time=13:45:59` contract；实现使用
  `13:46:00` exclusive upper bound。
- 截止放在 sequential dump reader，而不是 message decoder。跨过边界后整个文件
  停止读取，因此盘后 I010/I011、快照和损坏尾帧都不会影响日盘发布。
- 输入按 capture 顺序跨越日盘边界；第一条盘后 header 之后即使出现较早 event time，
  也按用户要求视为盘后尾部而不再接纳。
- 这是统一研究时段 cutoff，不是交易所全部商品的逐 `FLOW-GROUP` session 模型；
  group 5/6/9 在 13:46 后的消息同样排除。

## 实施步骤

1. 为测试 frame builder 增加可指定 `INFORMATION-TIME` 的参数。
2. 增加 dump-level 回归：验证 13:45:59 消息进入 CSV，14:40 的冲突 I010 和其后
   损坏 bytes 不被处理，日盘 basic-info 保持不变。
3. 在 reader 解码 header 后、读取 body 前检查 exclusive cutoff；记录是否命中边界
   及停止 byte offset，并在 CLI 完成日志输出。
4. 更新 TAIFEX 专题文档、测试说明与 converter onboarding。

## 验证策略

- 先证明新增 focused test 在旧实现上因 I010 conflict 失败。
- 运行 `taifex_converter_tests` 和完整 CTest。
- 用真实 `20260107` dump dry-run，确认不再触发 14:40 的 I011 conflict，并且停止
  offset 早于文件末尾。
- 用 `20260525` 或 `20260526` dry-run，判定截断发生在截止前还是截止后；只有盘后
  截断应被硬截止隔离，日盘内截断仍须失败。
- 运行 `git diff --check` 并审查完整 diff、日志与错误路径。

## 回滚

回滚 reader cutoff、对应统计字段、测试及文档即可恢复原先 exact-EOF 全文件处理；CSV
schema 不变，无需数据迁移，但按新 contract 生成的 CSV 需要重新转换。

## 风险

- 截止会有意丢弃 13:46 后仍处于一般交易时段的特殊 flow group 数据。
- 若 dump capture 顺序在边界附近严重逆序，首次跨界后到达的迟到日盘消息也会被丢弃；
  真实 dump 验证需记录边界 offset 和转换结果。

## 实施结果

- focused test 已证明旧实现会处理 14:40 I010 并因 conflict 失败；加入 reader cutoff
  后通过，且 13:45:59.999999 的盘口仍输出。
- `20260107` 与 `20260407` 真实 dry-run 均在第一条 13:46 消息处成功停止，原
  I011/I010 conflict 不再发生。
- `20260707` 日盘 dry-run 成功，读取 61,605,862 条消息、模拟 54,086,067 行 depth，
  cutoff offset 为 `5640462381`。
- `20260525/26` 的截断发生在约 13:05，属于日盘内损坏，仍按 strict frame contract
  失败；本计划没有把它们误归类为盘后尾部。
