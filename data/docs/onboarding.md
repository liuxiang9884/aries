# Data 模块 Onboarding

更新时间：2026-08-01

## 模块职责

负责原始数据获取、清洗、转换、schema、分区、质量检查、版本和 manifest。

## 当前状态

- 台湾 raw 数据通过 `scripts/` 下载到仓库外目录，支持断点续传和 cron 调度。
- 2026-07-07 TAIFEX 与 TWSE dump 已由 Orion 转换为 legacy CSV 并记录校验摘要。
- Aries 已实现 TWSE / TPEx converter 的 legacy depth/basic-info contract，以及
  TAIFEX 全 futures 45 列 depth 与 27 列 basic-info research schema；尚未建立统一
  manifest、跨市场 schema version 或夜盘交易日历 contract。

## 关键入口

- 下载脚本：`scripts/pull-tw-raw`、`scripts/synology-pull`
- converter 子模块：`data/converter/`
- 数据现状与复现证据：`data/docs/data.md`
- 交易所规格：`data/docs/exchange/`
- 数据下载与文件检查：`data/docs/testing.md`

## 重要边界 / Contract

- raw、dump、CSV 和后续研究数据的时间、单位、来源与版本必须显式记录。
- 当前 CSV 是 Orion 兼容输出，不是正式版本化研究数据集。
- 数据和大体积转换结果不进入 git；仓库只跟踪代码、schema、manifest 和校验摘要。

## 验证命令

数据下载与文件检查见 `data/docs/testing.md`；项目级检查见
`docs/testing.md`；converter 验证见 `data/converter/docs/testing.md`。

## 当前主线

当前具体工作位于 `data/converter`，由其 onboarding 维护状态和下一步。

## 下一步

统一 raw、dump、CSV 与后续研究数据的目录和版本规则，建立第一版 manifest，并在
批量 TAIFEX 转换前锁定夜盘 timestamp 映射。

## 按需阅读

- 数据路径、转换结果和 hash：`data/docs/data.md`
- converter 接手入口：`data/converter/docs/onboarding.md`
