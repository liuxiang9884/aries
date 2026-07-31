# 模块化文档与 Onboarding 迁移计划

## 目标

- 将模块专属文档迁入对应模块的 `docs/`，根 `docs/` 只保留项目级和跨模块文档。
- 为项目根目录和当前已确定的模块建立统一的 `docs/onboarding.md`。
- 让新对话默认只读取全局 onboarding 与当前最具体模块的 onboarding，再按链接加载专题文档。
- 在 `AGENTS.md` 中简要固化文档定位、启动读取和结束更新规则。

## 非目标

- 不修改 converter、数据下载、构建或测试行为。
- 不重新设计数据 schema、TWSE CSV contract 或研究工作流。
- 不为 `scripts`、`configs`、`tests`、`cmake` 等支持目录创建 onboarding。
- 不把已完成计划或专题文档内容复制进 onboarding。

## 关键决定

- 一级模块为 `data`、`factors`、`backtest`、`models`、`research`；具有独立职责、构建入口和测试的 `data/converter` 是二级模块。
- 根和模块 onboarding 统一命名为 `docs/onboarding.md`。
- 当前主线指向最具体模块；新对话不自动读取其父模块 onboarding，必要 contract 由当前模块 onboarding 显式链接。
- onboarding 统一包含模块职责、当前状态、关键入口、重要边界 / contract、验证命令、当前主线、下一步和按需阅读；空模块使用最小版本。
- README 保存稳定用途和使用方式，onboarding 保存动态状态与下一步。
- 根测试文档保留项目级构建、完整回归和通用规范；converter focused 验证迁入 converter 模块。
- 已提交计划保留在所属模块的 `docs/plans/`；只有仍有效或正在执行的计划才从 onboarding 链接。

## 迁移范围

1. 将根 onboarding 迁移为 `docs/onboarding.md`，压缩为全局导航和唯一当前主线。
2. 创建 data 模块 onboarding，迁移数据说明、交易所规格和 data 验证说明。
3. 创建 converter 模块 onboarding 与测试说明，迁移 TWSE 专题和 converter 计划。
4. 为 `factors`、`backtest`、`models`、`research` 创建最小 onboarding。
5. 更新 `README.md`、`AGENTS.md` 及仓库内所有旧路径引用。

## 验证策略

- 使用 `rg` 确认没有遗留旧文档路径。
- 检查每个约定模块均有 `docs/onboarding.md`，支持目录没有被误建 onboarding。
- 解析仓库内 Markdown 相对链接，确认目标文件存在。
- 运行 `git diff --check`，审查 rename、文档内容和最终目录树。
- 本任务不修改代码，不需要重新构建或运行 converter 数据验证。

## 回滚

- 迁移仅涉及已跟踪文档和链接；回滚本任务原子提交即可恢复原路径。
- 不移动、覆盖或生成仓库外的数据和实验产物。

## 风险与处理

- 路径迁移可能留下失效链接：通过全仓 `rg` 和 Markdown 链接检查覆盖。
- 多级 onboarding 可能重复事实：全局只保留导航和跨模块状态，模块事实只写在所属模块。
- 动态状态可能长期失真：结束对话时只更新当前模块；仅当主线或跨模块状态变化时更新全局。

## 验证结果

- `AGENTS.md` 只保留文档分层、按需读取和更新责任规则，没有写入模块内容或专题导航。
- 全局、data、data/converter、factors、backtest、models、research onboarding
  均存在，并包含当前状态、验证命令、当前主线、下一步和按需阅读。
- 数据说明、交易所规格、data / converter focused test、converter 专题和既有
  converter 计划已迁入所属模块；根 `docs/` 仅保留项目级 onboarding、测试
  规则、跨模块计划和项目级 skill 事实源。
- 全仓检查未发现对旧 onboarding、data、TWSE 专题、exchange 或 converter
  计划路径的遗留引用。
- 文档结构检查和 `git diff --check` 均通过；本任务未修改代码或数据处理行为，
  因此未重新运行 CMake、CTest 或完整 dump 转换。
