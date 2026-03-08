# parser/third_party — 第三方依赖

解析引擎**必须**使用本目录中的 LLVM/Clang 预编译库进行开发与构建。

## 目录布局

- `llvm/` — 解压后的 LLVM+Clang 预编译包（由 `download_llvm.sh` 生成）。
  - 典型结构：`llvm/bin`、`llvm/include`、`llvm/lib`、`llvm/lib/cmake/llvm`、`llvm/lib/cmake/clang`。
- `download_llvm.sh` — 根据当前平台下载并解压预编译包到 `llvm/`。

## 使用方式

1. **首次构建前**在 `parser` 目录下执行：
   ```bash
   ./third_party/download_llvm.sh
   ```
   脚本会根据 OS/架构选择 [awakecoding/llvm-prebuilt](https://github.com/awakecoding/llvm-prebuilt) 的 `clang+llvm-18.1.8-*` 包，下载并解压到 `third_party/llvm`。

2. **CMake 约定**：`parser/CMakeLists.txt` 会优先检测 `parser/third_party/llvm`：
   - 若存在 `third_party/llvm/lib/cmake/llvm/LLVMConfig.cmake`，则设置 `LLVM_DIR` 并执行 `find_package(LLVM)` / `find_package(Clang)`；
   - 解析引擎将链接 Clang libtooling 相关组件，并定义 `CODEXRAY_HAVE_CLANG`。

3. **不下载时**：若未运行脚本、且系统未设置 `LLVM_DIR`，则 parser 仍可构建为“无 Clang”骨架（AST 等为占位实现）。

## 支持平台

| 平台        | 架构   | 预编译包名（示例）                    |
|-------------|--------|----------------------------------------|
| macOS       | ARM64  | clang+llvm-18.1.8-aarch64-macos.tar.xz |
| macOS       | x86_64 | clang+llvm-18.1.8-x86_64-macos.tar.xz  |
| Linux       | ARM64  | clang+llvm-18.1.8-aarch64-ubuntu-22.04.tar.xz |
| Linux       | x86_64 | clang+llvm-18.1.8-x86_64-ubuntu-22.04.tar.xz  |

其他平台需自行从 [llvm-prebuilt releases](https://github.com/awakecoding/llvm-prebuilt/releases) 下载对应包并解压到 `third_party/llvm`。
