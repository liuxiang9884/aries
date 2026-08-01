# Aries 全局 Onboarding

更新时间：2026-08-01

## 项目职责

`aries` 是日内策略研究仓库，覆盖数据处理与存储、因子计算与验证、模型构造、
回测和研究实验复现。目录按业务模块组织，不增加 Python / C++ 语言层。

## 当前状态

- 根 CMake、preset 和 vcpkg manifest 已提供统一的 configure、build 与 CTest
  入口。
- 数据模块已具备台湾 raw 数据下载工具；TWSE / TPEx converter 可生成
  Orion-compatible depth 与 format1 basic-info CSV；TAIFEX converter 可同步生成
  全 futures research depth 与 basic-info CSV，且已完成 2026-07-07 真实数据验证。
- 因子、回测、模型和研究模块目前只有目录骨架，尚无正式行为 contract。
- 大体积 raw、CSV、临时转换结果和实验产物均保存在仓库外。

## 模块入口

| 模块 | Onboarding | 当前状态 |
|---|---|---|
| data | `data/docs/onboarding.md` | raw 数据与数据 contract |
| data/converter | `data/converter/docs/onboarding.md` | 当前主线 |
| factors | `factors/docs/onboarding.md` | 骨架 |
| backtest | `backtest/docs/onboarding.md` | 骨架 |
| models | `models/docs/onboarding.md` | 骨架 |
| research | `research/docs/onboarding.md` | 骨架 |

## 项目级边界

- 数据、时间、因子、标签、成交和评估口径必须有明确事实源与可复现验证。
- README 记录稳定的构建和使用入口；动态进度与下一步只写 onboarding。
- 模块细节写入所属模块，根 `docs/` 只保留跨模块规则、项目级验证和计划。
- 不把 NAS 凭据、机器专用配置、大数据文件、模型权重或缓存纳入 git。

## 验证命令

```bash
git status --short --branch
git log --oneline -8
git diff --check
VCPKG_ROOT=/home/liuxiang/vcpkg cmake --preset debug \
  -DFETCHCONTENT_SOURCE_DIR_NOVA=/home/liuxiang/dev/nova
VCPKG_ROOT=/home/liuxiang/vcpkg cmake --build --preset debug
VCPKG_ROOT=/home/liuxiang/vcpkg ctest --preset debug
```

## 当前主线

当前最具体模块是 `data/converter`。新对话在读取本文件后，直接读取
`data/converter/docs/onboarding.md`，不默认读取其他模块 onboarding。

## 下一步

建立数据 manifest/version contract，并继续讨论 TAIFEX 缺口数据的下游使用规则。
TAIFEX 当前只处理确认过的日盘输入；夜盘支持暂缓。

## 按需阅读

- 稳定构建与工具用法：`README.md`
- 项目级验证规范：`docs/testing.md`
- 当前跨模块文档迁移记录：`docs/plans/2026-07-31-module-onboarding.md`
