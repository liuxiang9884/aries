# 模块文档目录迁移计划

日期：2026-08-05

状态：执行中

## 目标

统一 `aries` 的文档分层：项目级文档保留在根 `docs/`；每个顶层业务模块只维护
一个 `docs/` 目录；子模块文档放在该顶层模块 `docs/` 下的同名目录。首个迁移对象为
`data/converter/docs/`，目标路径是 `data/docs/converter/`。

## 非目标

- 不修改 converter 的实现、schema、构建、测试或运行行为。
- 不移动 `data/docs/exchange/` 中的官方交易所文档。
- 不为 `cmake/`、`configs/`、`scripts/`、`tests/` 等支持目录机械创建 onboarding。
- 不改写专题文档内容，仅更新目录规则、路径引用和必要的接手说明。

## 关键决定

- 顶层业务模块当前包括 `data`、`factors`、`backtest`、`models` 和 `research`；各自保留
  唯一的 `<module>/docs/`。
- 具备独立职责的子模块仍保留自己的 onboarding，但放在
  `<top-level-module>/docs/<submodule>/onboarding.md`。
- converter 的全部专题文档与历史计划整体迁移到 `data/docs/converter/`，保持文件名和
  `plans/` 层次不变。
- 全仓库使用新路径作为唯一事实源，不保留旧路径副本或兼容链接。

## 影响边界

- `AGENTS.md` 的文档分层和新对话启动规则。
- 根、data 与 converter onboarding。
- README、项目级验证说明、data 专题文档及 converter 内部交叉引用。
- Git 中的 converter 文档路径；代码和外部数据路径不受影响。

## 实施步骤

1. 将 `data/converter/docs/` 整体移动到 `data/docs/converter/`。
2. 全仓库替换旧的 `data/converter/docs/...` 引用。
3. 更新 `AGENTS.md`，明确顶层模块唯一 `docs/` 与子模块目录规则。
4. 更新全局、data、converter onboarding 的入口与当前路径事实。
5. 检查所有仓库内 markdown 路径、重复专题和遗留 `docs/` 目录。

## 验证策略

- `git diff --check`
- 确认 `data/converter/docs/` 不存在且 `data/docs/converter/onboarding.md` 存在。
- 使用 `rg` 确认没有 `data/converter/docs` 或其他旧 converter 文档路径。
- 从全部 Markdown 中提取反引号包裹的仓库相对路径，对可判定路径执行存在性检查。
- 运行 `git status --short --branch` 并检查 rename diff，确认没有代码文件变化。

## 回滚方案

本次仅修改 Git 跟踪的文档，可通过逆向 rename 和恢复路径引用完整回滚；不涉及外部数据、
生成文件或不可逆迁移。

## 未决风险

- 历史计划中的旧路径也必须改成新路径，否则新对话可能沿用过期入口。
- 文档中的命令、外部绝对路径和概念性占位符不能机械地按文件路径验证，需要区分检查。
