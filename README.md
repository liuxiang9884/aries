# aries

`aries` 是日内策略研究仓库。当前代码模块按业务领域组织，数据 dump 转换入口
位于 `data/converter/`。

## 构建

项目要求 CMake 3.28、Ninja 和 vcpkg。首次配置前设置 vcpkg 根目录：

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

本地开发 Nova 时，可以覆盖固定的远端源码：

```bash
export VCPKG_ROOT=/home/liuxiang/vcpkg
cmake --preset debug \
  -DFETCHCONTENT_SOURCE_DIR_NOVA=/home/liuxiang/dev/nova
```

仓库提交 `CMakePresets.json` 和 `vcpkg.json`；机器专用设置应写入未跟踪的
`CMakeUserPresets.json`，不要写入公共 CMake 文件。

当前 CMake 工程只建立公共构建、依赖和测试入口，尚未包含 TWSE 或 TAIFEX
converter 实现。
