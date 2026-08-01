# Backtest 模块 Onboarding

更新时间：2026-08-01

## 模块职责

负责信号对齐、持仓生成、撮合、交易成本、资金约束、风险控制和绩效归因。

## 当前状态

- 模块尚无实现、回测 contract 或 smoke 入口。
- 已完成 `hftbacktest` 基线 commit 的结构、文档和核心代码调研；该调研是设计输入，
  尚未成为 `aries` contract。

## 关键入口

- 当前仅有 `backtest/` 目录骨架。
- 外部参考实现调研：`backtest/docs/hftbacktest-reference.md`

## 重要边界 / Contract

必须明确 signal、order 与 fill 的时间顺序，以及手续费、滑点、冲击、涨跌停、
停牌和流动性约束。

## 验证命令

尚未建立；首次实现时需提供无未来数据和成交边界 smoke。

## 当前主线

已进入首次架构设计准备；先以 `hftbacktest` 调研为事实基线，再锁定 `aries` 自身的
目标、事件时间线和验证边界。

## 下一步

使用 `grill-me-enhanced` 锁定首个研究场景、事件时间线、成交假设、成本模型、资金
约束、策略 API 和最小验证样例，再进入 adversarial plan review。

## 按需阅读

项目级回测与模型验证原则见 `AGENTS.md`。
`hftbacktest` 的可借鉴结构与不可直接继承边界见
`backtest/docs/hftbacktest-reference.md`。
