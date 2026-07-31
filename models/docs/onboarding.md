# Models 模块 Onboarding

更新时间：2026-07-31

## 模块职责

负责特征集合、label、训练 / 验证 / 测试切分、模型训练、评估、解释和推理产物。

## 当前状态

模块尚无实现、训练 contract 或评估入口。

## 关键入口

当前仅有 `models/` 目录骨架。

## 重要边界 / Contract

必须显式记录 label、feature set、时间切分、随机种子、baseline、指标和最终
holdout，禁止测试集反复参与模型选择。

## 验证命令

尚未建立；首次实现时需提供确定性 smoke 和可复现 baseline。

## 当前主线

尚未启动。

## 下一步

首次模型工作前先锁定 label、样本切分、baseline 和评价指标。

## 按需阅读

项目级回测与模型验证原则见 `AGENTS.md`。
