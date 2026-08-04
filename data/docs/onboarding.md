# Data 模块 Onboarding

更新时间：2026-08-04

## 模块职责

负责原始数据获取、清洗、转换、schema、分区、质量检查、版本和 manifest。

## 当前状态

- 台湾 raw 数据通过 `scripts/` 下载到仓库外目录，支持断点续传和 cron 调度。
- 2026-07-07 TAIFEX 与 TWSE dump 保留 Orion legacy CSV 校验摘要，仅用于历史对照。
- Aries 已实现 TWSE / TPEx 37 列 `twse-orderbook-v2` 和 30 列
  format1 basic-info contract，并通过 2026-07-07 全日真实数据验证。TAIFEX 当前为全
  futures 44 列 orderbook 与 27 列 basic-info research schema。
- `/tw_backup` 已有股票 CSV 在 v2 历史重建前仍可能是 23 列 legacy；尚未建立
  统一 manifest、跨市场 schema version 或夜盘交易日历 contract。

## 关键入口

- 下载脚本：`scripts/pull-tw-raw`、`scripts/synology-pull`
- converter 子模块：`data/converter/`
- 数据现状与复现证据：`data/docs/data.md`
- 交易所规格：`data/docs/exchange/`
- 数据下载与文件检查：`data/docs/testing.md`

## 重要边界 / Contract

- raw、dump、CSV 和后续研究数据的时间、单位、来源与版本必须显式记录。
- TWSE 以 `twse-orderbook-v2` exact header 为当前代码 contract；不提供 23 列
  legacy writer/reader 或双写。正式研究数据集仍需建立 manifest 与 schema version。
- 数据和大体积转换结果不进入 git；仓库只跟踪代码、schema、manifest 和校验摘要。

## 验证命令

数据下载与文件检查见 `data/docs/testing.md`；项目级检查见
`docs/testing.md`；converter 验证见 `data/converter/docs/testing.md`。

## 当前主线

当前具体工作位于 `data/converter`，由其 onboarding 维护状态和下一步。

## 下一步

先在 TWSE v2 实现合并后重建 `/tw_backup/data/tw/csv/stock/`，通过 exact
header 、每日 summary 和问题日志验收；再统一 raw、dump、CSV 与后续研究数据的目录/
版本规则并建立第一版 manifest。TAIFEX reader 在第一条 `13:46:00` 消息处停止，
只发布统一日盘窗口；夜盘支持暂缓。

## 按需阅读

- 数据路径、转换结果和 hash：`data/docs/data.md`
- converter 接手入口：`data/converter/docs/onboarding.md`
