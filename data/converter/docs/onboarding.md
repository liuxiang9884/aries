# Data Converter 模块 Onboarding

更新时间：2026-07-31

## 模块职责

负责将交易所原始 dump 确定性地转换为可校验的数据文件，并维护协议、时间、单位、
输出 schema 和兼容性边界。

## 当前状态

- `twse_dump_converter` 已支持 TWSE listed 与 TPEx / OTC 的 format 1、6、17、
  22、23，以及五种 Orion-compatible filter mode。
- 2026-07-07 `stock` 完整输出与 Orion legacy CSV bytes、行数和 SHA-256 一致。
- `data/converter/taifex/` 仍为待提取骨架。
- TWSE 协议复核已发现若干待逐项讨论的问题；已确认 format 1/22 的 `AL/NE`
  控制记录不影响当前普通股票 CSV，先记录而不作为最高优先级修复。

## 关键入口

- 实现：`data/converter/twse/`
- 测试：`tests/data/converter/twse/`
- 协议与 Orion 差异：`data/converter/docs/twse.md`
- 验证命令：`data/converter/docs/testing.md`

## 重要边界 / Contract

- 当前 23 列 CSV 是 Orion legacy compatibility contract，不直接升级为正式研究 schema。
- 交易日按 UTC+8 自然日零点解释；offline `localtime = exchtime`、
  `symbol_id = -1`。
- legacy CSV 丢弃 bid / ask volume；正式 schema 必须另行设计和版本化。
- 所有有意偏离 Orion 的行为必须记录在 TWSE 专题文档并有 focused test。

## 验证命令

```bash
VCPKG_ROOT=/home/liuxiang/vcpkg \
  cmake --build --preset debug --target twse_converter_tests
ctest --test-dir build/debug --output-on-failure \
  -R '^(BcdDecoderTest|MessageDecoderTest|DumpConverterTest)'
git diff --check
```

## 当前主线

逐项核对 TWSE / TPEx converter 与交易所规格，区分当前行情缺陷、legacy
兼容行为和正式研究 schema 的后续工作。

## 下一步

继续讨论 warrant 证券代码规则并按结论决定是否修复；完成 TWSE / TPEx
converter 审查后，再按同级结构提取 TAIFEX converter，并建立小样本和完整
文件兼容性证据。

## 按需阅读

- TWSE / TPEx 协议与已知边界：`data/converter/docs/twse.md`
- focused 与完整 dump 验证：`data/converter/docs/testing.md`
- 数据路径与发布结果：`data/docs/data.md`
- 已完成计划：`data/converter/docs/plans/`
