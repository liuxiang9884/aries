# Data Converter CMake Scaffold 计划

## 目标

- 为 Aries 建立唯一的根 CMake configure/build/test 入口。
- 将项目级依赖、编译选项和 vcpkg 配置放在仓库根层管理。
- 建立 `data/converter/{twse,taifex}`、对应配置和测试目录骨架。
- 固定 Nova 版本，同时允许本机用 `FETCHCONTENT_SOURCE_DIR_NOVA` 覆盖源码路径。

## 非目标

- 本阶段不移植 Orion 的 TWSE 或 TAIFEX 转换实现。
- 不定义 CSV schema、数据版本或持久化 contract。
- 不增加占位业务 target、示例程序或虚假测试。
- 不引入 `include/`、`src/` 或语言层目录。

## 关键决定

- 根 `CMakeLists.txt` 只负责项目设置、公共模块、CTest 和子目录编排。
- 公共逻辑放在 `cmake/dependencies.cmake` 与
  `cmake/project_options.cmake`。
- vcpkg 使用根 `vcpkg.json` manifest；toolchain 由
  `CMakePresets.json` 在 `project()` 前传入，不在 CMake 中硬编码本机路径。
- Nova 固定到已验证的提交
  `aa1b9f562f41e78442cef350c4fed01ecf33e8e8`。
- Nova 以 `SYSTEM EXCLUDE_FROM_ALL` 子项目接入；configure 时解析依赖，但没有
  Aries consumer target 时不进入默认 build。
- 模块头文件和实现文件共同放在市场目录中。

## 实现步骤

1. 增加根 CMake、preset、vcpkg manifest 和公共 CMake 模块。
2. 增加 converter、配置和 C++ 测试目录骨架。
3. 让没有业务源码的 scaffold 可以完成 configure、build 和 CTest。
4. 更新项目文档中的构建入口和当前目录事实。

## 验证策略

- 记录修改前因缺少根 `CMakeLists.txt` 而 configure 失败。
- 使用本机 Nova source override 执行 fresh `cmake --preset debug`。
- 执行 `cmake --build --preset debug` 和 `ctest --preset debug`。
- 执行现有 Python 单元测试、`git diff --check` 和最终 diff review。

## 回滚

- 本阶段只新增构建 scaffold 和目录占位；回滚对应原子提交即可，不涉及数据
  或外部状态迁移。

## 未决风险

- TWSE converter 的实际依赖集可能小于 Nova 的完整依赖集；移植时再基于目标
  源码收缩依赖，不在本阶段猜测删除。

## 验证结果

- 修改前 fresh configure 返回 `1`，原因是仓库缺少根 `CMakeLists.txt`。
- Debug preset 使用本地 `FETCHCONTENT_SOURCE_DIR_NOVA` 完成 fresh configure；
  vcpkg manifest mode 生效。
- 不使用本地 override 的 fresh configure 成功，Nova checkout 精确为
  `aa1b9f562f41e78442cef350c4fed01ecf33e8e8`。
- `CLI11`、`fmt`、`magic_enum`、`nameof`、`quill`、`tomlplusplus` 和
  `yyjson` 均解析到 fresh build 自己的 `vcpkg_installed`。
- 两个 fresh build 的默认构建均为 `ninja: no work to do.`，确认 Nova 不会在
  没有 consumer target 时进入 `ALL`。
- 显式执行 `cmake --build --preset debug --target nova --parallel 8` 成功生成
  `libnova.a`，确认 Nova target 可供后续 converter 链接。
- `ctest --preset debug` 和独立 Python unittest 均通过；CTest 当前注册
  `synology_pull`，其内部执行 4 个测试。
