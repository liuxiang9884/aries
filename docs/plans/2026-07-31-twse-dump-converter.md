# TWSE Dump Converter 提取计划

## 目标

- 从 Orion `4282286` 提取 TWSE offline dump 转换所需的最小协议和业务逻辑。
- 支持 format 1、6、17、22、23，以及 `stock`、`etf`、`warrant`、
  `odd_lot`、`all` 五种 filter mode。
- 生成与 Orion 当前 23 列 CSV contract 兼容的结果。
- 对截断、非法 BCD、非法 message length 和超过五档的消息显式失败。
- 提供单一 `twse_dump_converter` CLI、dry-run、统计摘要和原子输出。

## 非目标

- 不提取 multicast、replay、SHM、symbol pool 或 Orion binary data file。
- 不转换 Orion 当前未处理的 TWSE format。
- 本阶段不增加 bid/ask volume 列，不把现有 CSV 升级为新的正式数据版本。
- 不处理 TAIFEX；TAIFEX 保持后续同级模块。

## 数据与时间 contract

- dump 是连续 BCD message；每条消息由 10-byte header 和
  `message_length - 10` byte body 构成。
- `trading_day` 使用 `Asia/Taipei` 固定 UTC+8 的自然日零点计算，不依赖进程
  时区；BCD `HHMMSSmmmuuu` 作为当日偏移。
- offline CSV 的 `symbol_id` 固定为 `-1`，`localtime` 等于 `exchtime`。
- CSV 字段、顺序和两位小数格式保持 Orion 当前输出；五档 volume 只用于内部
  解码，不进入该 legacy CSV。
- 每个 symbol 维护跨消息状态，包括涨跌停、昨收、open、last、累计 volume
  和 Orion 当前增量估算的 `total_value`。

## 设计决定

- 源码放在 `data/converter/twse/`，头文件和实现文件同目录。
- wire layout 使用 byte span、显式 offset 和 bit mask，不依赖 C++ bitfield
  顺序。
- decoder 接收有边界的 byte span；动态档位数量先校验再读取。
- CSV 先写同目录 `.partial.<pid>`，flush/close 成功后 rename 到最终路径；
  默认拒绝覆盖，`--overwrite` 显式允许替换。
- CLI 直接接收 `--dump`、`--output`、`--trading-day` 和
  `--symbol-filter-mode`，避免为单一 offline 工具继承 Orion realtime TOML。
- C++ focused tests 使用 GoogleTest；大文件和转换输出仍写入
  `/home/liuxiang/tmp`。

## 实现步骤

1. 先增加 BCD、message decoder 和 dump converter 的失败测试及 CMake
   targets，证明实现缺失。
2. 实现 protocol、BCD decoder、归一化 depth state 和 symbol filters。
3. 实现有边界检查的 message decoder、dump framing 和统计。
4. 实现 legacy CSV writer、原子发布与 CLI。
5. 完成 synthetic fixture tests、malformed/truncated tests 和 CLI smoke。
6. 使用 2026-07-07 stock dump 做完整转换，与 Orion 输出比较 header、行数、
   bytes 和 SHA-256；不一致时定位到首个差异。
7. 更新 `README.md`、`docs/data.md`、`docs/testing.md` 和 onboarding。

## 验证门

- fresh Debug configure/build。
- TWSE focused CTest 和现有 Synology Python tests。
- malformed/truncated input 不发布最终 CSV，不遗留 partial。
- dry-run 不创建输出文件。
- 2026-07-07 `stock` full dump 兼容性比较。
- `git diff --check`、完整 diff review 和原子提交。

## 回滚

- 新 converter 不替换现有数据文件或 Orion 工具；回滚本任务提交即可。
- full-dump 验证只写 `/home/liuxiang/tmp`，不覆盖已发布 CSV。

## 未决风险

- Orion 的 CSV 不包含五档 volume，当前兼容输出仍会丢失该信息；正式 schema
  需另行设计和版本化。
- Orion `stock` filter 会接受四字符 ETF；本阶段为兼容性保留该语义。
- 2026-07-07 stock dump 中没有可供 `odd_lot` / `warrant` 输出的真实消息，
  这两个 mode 当前仅由 synthetic fixture 覆盖。

## 验证结果

- Debug CTest 共 19 项通过，覆盖 BCD、header、format 1 / 6 / 17 / 22 /
  23、五种 filter mode、非法日期、截断输入、超过五档、dry-run、默认拒绝
  覆盖、partial symlink 和原子发布。
- 2026-07-07 stock dump dry-run 读取 25,993,761 条消息、输出
  15,886,026 行、维护 1,979 个 symbol，读取 3,153,093,917 bytes。
- Aries 生成文件与 Orion 已发布 CSV 均为 2,742,684,274 bytes（含 header
  共 15,886,027 行），SHA-256 均为
  `f5981991517c24d07fbe4ee2ef38d9b9d3d198b69d2c841cdab39d5a8cb3cc41`，
  `cmp` 返回 0。
- 完整 dump 的 `etf`、`warrant`、`odd_lot`、`all` dry-run 均成功；其中
  format 6 末尾的 `000000` / `999999999999` 控制消息被显式忽略，避免
  Orion 的 `warrant` / `all` 路径把控制消息当成非法行情时间。
