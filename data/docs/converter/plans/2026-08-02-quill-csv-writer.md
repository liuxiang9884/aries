# Nova / Quill CSV writer 迁移计划

日期：2026-08-02

状态：已完成

## 目标

- 将 TWSE / TPEx 与 TAIFEX converter 的 CSV 行格式化和写入后端统一迁移到
  `quill::CsvWriter<Schema, nova::LogManager::NovaFrontendOptions>`。
- 保持当前 `Orderbook<5>`、basic-info record、CSV 列顺序、字段语义、数值格式和
  CLI 参数不变。
- 保留同目录 partial、显式 flush/close、双输出 backup/rename/rollback 和
  `--overwrite` 边界，不保留 legacy writer 双轨。

## 非目标

- 不实施尚待迁移的统一 `Orderbook<N>` schema，不增加 Trade 或市场特有分表。
- 不修改 decoder、orderbook builder、时间戳、volume/value 或交易日口径。
- 不重建历史 CSV，不改变批处理目录和文件命名。

## 关键决定

- compile-time `Schema::header` / `Schema::format` 是 CSV 列 contract 的代码事实源；
  writer 通过 `append_row()` 直接传递 typed fields，不先拼接整行字符串。
- Quill 使用 Nova frontend options；converter CLI 继续由 Nova 初始化并停止 backend，
  core writer 要求调用方已经启动 Nova logging。测试进程显式提供同样的生命周期。
- Quill 只写 `.partial.<pid>`，`flush()` 并销毁 writer 后才允许 rename 发布，避免异步
  backend 继续写入已经发布的路径。
- 保留现有路径类型、覆盖冲突、双文件回滚和 partial 清理检查。Quill backend 的异步
  I/O 错误不能同步从 `append_row()` 返回；发布前仍检查 partial 为 regular file 且存在，
  文档明确这一边界。

## 影响边界

- `data/converter/twse/csv_writer.*`
- `data/converter/taifex/csv_writer.*`
- `data/converter/CMakeLists.txt`
- converter 测试进程的 Nova logging 生命周期
- converter onboarding、TWSE/TAIFEX contract 与测试文档

## 实施步骤

1. 用现有端到端 fixture 锁定迁移前 TWSE/TPEx 与 TAIFEX CSV bytes。
2. 定义现有 schema 对应的 compile-time header/format，并以 Nova frontend 的 Quill
   writer 替换 `ofstream + fmt::memory_buffer`。
3. 保留并复用双文件 staged publication；在 flush/close 后检查 partial，再执行 rename。
4. 为测试 binary 初始化 Nova backend，补充重复转换和 flush/close 后可读的回归覆盖。
5. 更新文档，删除“Aries 不采用 Quill”的错误描述，同时明确 schema 本轮不变。
6. 运行 focused tests、完整 CTest、格式/差异检查，并用一日真实 dump 做 smoke。

## 验证策略

- 迁移前后现有 fixture 生成的 CSV 内容逐 byte 不变。
- TWSE、TPEx、TAIFEX converter 单元及端到端测试全部通过。
- 覆盖 overwrite、拒绝目录/symlink、失败保留旧输出、partial 清理和双输出发布。
- 构建两个 converter CLI 并运行完整 CTest。
- 使用现有单日真实 dump 转换到 `/home/liuxiang/tmp`，核对退出状态、输出存在性、header、
  行数和 converter 日志；不覆盖正式数据。

## 回滚

单个迁移提交可整体 revert，恢复同步 `ofstream + fmt` writer；CSV schema 与调用接口不变，
不需要数据迁移。

## 风险

- Quill backend 异步执行格式化和磁盘写入，必须保证 backend 生命周期覆盖所有 writer，
  且 writer 在 rename 前完成 blocking flush/remove。
- Nova 默认 Quill backend error notifier 不把异步写错误重新抛给调用线程；磁盘耗尽等
  后端错误的可观测性弱于同步 `ofstream`。本轮保持日志可见和 staged publication，但不
  声称获得同步等价的错误传播。
- typed format 的细小差异可能改变历史 CSV bytes，必须以 fixture 和真实数据 smoke 检测。

## 验证结果

- Debug 构建通过；完整 CTest 73/73 通过。
- TWSE 与 TAIFEX 端到端 fixture 均逐 byte 锁定 orderbook/basic-info 输出。
- 2026-04-06 TWSE 完整 dump（429,426,360 bytes）读取 3,771,162 条消息，生成
  18,094 条 orderbook 和 44,915 条 basic-info；Quill 与迁移前 writer 的两份 CSV
  分别 `cmp` 相同。orderbook SHA-256 为
  `e418d8a570e598ddb53772938d59dd90d2c036a0777c99fdd8f5a9df1b6ce99c`，
  basic-info 为
  `d30463b60d01024b0c24349a504bb91a131a1849fda48d78de830b0e28624977`。
- 2026-02-11 TAIFEX dump 的 frame-aligned 67,109,260-byte 前缀读取 147,715 条消息，
  生成 23 条 orderbook 和 1,753 条 basic-info；Quill 与迁移前 writer 的两份 CSV
  分别 `cmp` 相同。orderbook SHA-256 为
  `2a5d7b09e46745d12f8dd8dbc1fc1439bcb3856b15460f603ad978b7a1672fab`，
  basic-info 为
  `6ba707e3489849f7e8b9babcd0586961d32ab9a8a9a039d6dd852be97ed887f1`。
- 真实数据 smoke 位于
  `/home/liuxiang/tmp/aries-quill-rows-smoke.jMbMOX/`，未覆盖正式 CSV。
