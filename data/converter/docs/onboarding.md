# Data Converter 模块 Onboarding

更新时间：2026-07-31

## 模块职责

负责将交易所原始 dump 确定性地转换为可校验的数据文件，并维护协议、时间、单位、
输出 schema 和兼容性边界。

## 当前状态

- `twse_dump_converter` 已支持 TWSE listed 与 TPEx / OTC 的 format 1、6、17、
  22、23，以及五种 Orion-compatible filter mode。
- converter 同时发布 Orion-compatible 23 列 depth CSV 与 30 列 format1
  basic-info CSV；后者按 `(trading_day, market, symbol)` 去重并排序。
- 2026-07-07 `stock` depth 输出与 Orion legacy CSV bytes、行数和 SHA-256
  一致；basic-info 输出 40,841 行，SHA-256 为
  `093699608154545fafe40337ad7616c029b3c5ae7ef1277b84cdfd4d349f540a`。
- `data/converter/taifex/` 仍为待提取骨架。

## 关键入口

- 实现：`data/converter/twse/`
- 测试：`tests/data/converter/twse/`
- 协议与 Orion 差异：`data/converter/docs/twse.md`
- 验证命令：`data/converter/docs/testing.md`

## 重要边界 / Contract

- 当前 23 列 CSV 是 Orion legacy compatibility contract，不直接升级为正式研究 schema。
- basic-info CSV 的主键、列顺序、空值、单位和 format1 重复处理以
  `data/converter/docs/twse.md` 为事实源；Big5/CP950 名称不解析、不输出。
- 交易日按 UTC+8 自然日零点解释；offline `localtime = exchtime`、
  `symbol_id = -1`。
- legacy CSV 丢弃 bid / ask volume；正式 schema 必须另行设计和版本化。
- 非 dry-run 必须提供 depth 与 basic-info 两个不同路径；运行时错误不发布半文件，
  `--overwrite` 下会恢复旧输出。进程崩溃或断电跨两次 rename 不具备文件系统事务保证。
- 所有有意偏离 Orion 的行为必须记录在 TWSE 专题文档并有 focused test。

## 验证命令

```bash
VCPKG_ROOT=/home/liuxiang/vcpkg \
  cmake --build --preset debug --target twse_converter_tests
ctest --test-dir build/debug --output-on-failure \
  -R '^(BasicInfoDecoderTest|BasicInfoCatalogTest|BcdDecoderTest|MessageDecoderTest|DumpConverterTest)'
git diff --check
```

## 当前主线

TWSE / TPEx format1 basic-info 与 legacy depth converter 已完成实现和完整 dump
回归，下一步转向同级 TAIFEX converter。

## 下一步

按同级结构提取 TAIFEX converter，先核对交易所文档与 Orion wire layout，再建立
小样本、错误边界和 2026-07-07 完整文件兼容性证据；实现期货基本资料时再统一
TWSE / TAIFEX 的 `volume`、`multiplier` 命名和单位 contract。

## 按需阅读

- TWSE / TPEx 协议与已知边界：`data/converter/docs/twse.md`
- focused 与完整 dump 验证：`data/converter/docs/testing.md`
- 数据路径与发布结果：`data/docs/data.md`
- 已完成计划：`data/converter/docs/plans/`
