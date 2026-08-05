# 验证说明

更新时间：2026-07-31T17:51:13+08:00

## 基础检查

当前仓库尚未建立 Python package、通用数据校验框架、回测 smoke 或模型训练
入口。根 CMake 工程通过 CTest 注册 Synology 下载测试和 TWSE converter
focused tests。提交前至少执行：

```bash
git status --short --branch
git log --oneline -8
git diff --check
```

## CMake Scaffold 检查

项目使用根 `CMakePresets.json` 和 vcpkg manifest。首次配置会在对应 build
目录安装 manifest 依赖。当前验证命令为：

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg

cmake --preset debug \
  -DFETCHCONTENT_SOURCE_DIR_NOVA=/home/liuxiang/dev/nova
cmake --build --preset debug
ctest --preset debug
```

`FETCHCONTENT_SOURCE_DIR_NOVA` 只用于本地联调；省略时使用
`cmake/dependencies.cmake` 固定的 Nova commit。机器专用值可以放在不纳入
git 的 `CMakeUserPresets.json`。

## 模块验证入口

模块专属 focused test、数据校验和完整文件兼容性命令由各模块自己的测试文档
维护：

- data 下载与文件检查：`data/docs/testing.md`
- data converter：`data/docs/converter/testing.md`

## 后续测试要求

- 新增数据读取、解压、转换或质量检查逻辑时，优先提供小样本 fixture 或 dry-run。
- 新增因子逻辑时，测试应覆盖输入对齐、滞后、缺失值、排序、窗口边界和无未来数据。
- 新增模型训练逻辑时，测试或 smoke 应记录 label、feature set、时间切分、随机种子和 baseline。
- 新增回测逻辑时，测试应覆盖 signal time、order time、fill time、成本模型和不能同 bar 偷看成交的边界。
