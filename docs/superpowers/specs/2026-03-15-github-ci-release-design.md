# CodeXray GitHub CI 发布设计规格

**日期**：2026-03-15
**状态**：待实现
**目标**：将项目同步到 GitHub，并通过 GitHub Actions 自动为三个平台打包含 parser 二进制的 `.vsix` 插件

---

## 1. 背景与目标

CodeXray 是一个 VSCode 插件，包含：

- **TypeScript 扩展**（`src/`）：VSCode 侧逻辑
- **前端 WebView**（`resources/ui/`）：Vite + TypeScript + AntV G6 v5
- **C++ Parser**（`parser/`）：C++17 + Clang libtooling + SQLite3 + Protobuf，依赖 LLVM 18.1.8 预编译包

**目标**：推送 `v*` tag 时，GitHub Actions 自动为以下三个平台构建并发布 `.vsix`：

| 平台           | LLVM 包                                      | vsce target     |
|---------------|----------------------------------------------|-----------------|
| macOS ARM64   | `clang+llvm-18.1.8-aarch64-macos.tar.xz`    | `darwin-arm64`  |
| Linux x86_64  | `clang+llvm-18.1.8-x86_64-ubuntu-22.04.tar.xz` | `linux-x64`  |
| Windows x64   | `clang+llvm-18.1.8-x86_64-windows.tar.xz`   | `win32-x64`     |

---

## 2. 仓库同步

### 2.1 .gitignore 调整

需要确保以下内容**被排除**（不提交到 Git）：

```
node_modules/
out/
*.vsix
.DS_Store
parser/third_party/llvm/
parser/third_party/clang/
parser/third_party/lld/
parser/third_party/*.tar.xz
parser/build/
resources/ui/node_modules/
resources/ui/dist/
resources/bin/
```

### 2.2 未追踪文件提交

当前未提交的新增文件：

- `resources/ui/src/graph/context-menu.ts`
- `resources/ui/src/graph/layout-worker-bridge.ts`
- `resources/ui/src/graph/layout-worker.ts`
- `resources/ui/src/graph/port-manager.ts`
- `resources/ui/src/graph/toolbar.ts`
- `resources/ui/src/graph/virtual-viewport.ts`
- `resources/ui/src/main.ts`
- `resources/ui/src/types.ts`
- `resources/ui/src/utils/perf.ts`
- `resources/ui/index.html`
- `resources/ui/package-lock.json`
- `resources/ui/tsconfig.json`
- `resources/ui/vite.config.ts`
- `resources/ui/.gitignore`
- `doc/` 下的新增文档（`ANSWERS_TO_10_QUESTIONS.md` 等）
- `.vscode/launch.json`、`.vscode/tasks.json` 的修改

所有这些文件均需提交后推送到 `origin main`。

---

## 3. GitHub Actions 工作流设计

### 3.1 工作流文件

路径：`.github/workflows/release.yml`

### 3.2 触发条件

```yaml
on:
  push:
    tags:
      - 'v*'
```

### 3.3 Job 架构

```
build-parser (matrix: 3 platforms, 并行)
  ├─ macos-14       → artifact: parser-darwin-arm64
  ├─ ubuntu-22.04   → artifact: parser-linux-x64
  └─ windows-latest → artifact: parser-win32-x64

package-vsix (needs: build-parser, 单 job)
  ├─ 下载三份 parser artifact → resources/bin/{target}/
  ├─ npm ci (root)
  ├─ npm run compile (tsc)
  ├─ cd resources/ui && npm ci && npm run build
  ├─ vsce package --target darwin-arm64
  ├─ vsce package --target linux-x64
  └─ vsce package --target win32-x64
  → artifact: vsix-packages (3个 .vsix)

create-release (needs: package-vsix)
  └─ gh release create $TAG 上传三个 .vsix
```

### 3.4 build-parser Job 细节

**matrix 定义**：

```yaml
strategy:
  matrix:
    include:
      - os: macos-14
        target: darwin-arm64
        llvm_suffix: aarch64-macos
        cmake_extra: ""
      - os: ubuntu-22.04
        target: linux-x64
        llvm_suffix: x86_64-ubuntu-22.04
        cmake_extra: ""
      - os: windows-latest
        target: win32-x64
        llvm_suffix: x86_64-windows
        cmake_extra: "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

**macOS 步骤**：
1. `brew install protobuf nlohmann-json sqlite3`
2. `bash parser/third_party/download_llvm.sh`
3. `cmake -B parser/build -S parser -DCMAKE_BUILD_TYPE=Release`
4. `cmake --build parser/build --target codexray-parser --config Release`
5. Upload artifact `parser-darwin-arm64`：`parser/build/codexray-parser`

**Linux 步骤**：
1. `sudo apt-get install -y libprotobuf-dev protobuf-compiler nlohmann-json3-dev libsqlite3-dev`
2. `bash parser/third_party/download_llvm.sh`
3. cmake 构建
4. Upload artifact `parser-linux-x64`：`parser/build/codexray-parser`

**Windows 步骤**：
1. `ilammy/msvc-dev-cmd` action 激活 VS 环境
2. `vcpkg install protobuf sqlite3 nlohmann-json` (使用 `VCPKG_ROOT` 环境变量，GitHub runner 自带)
3. 新增 `parser/third_party/download_llvm.ps1` PowerShell 脚本，下载 `x86_64-windows` 包，用 7z 解压
4. cmake 构建：`cmake -B parser/build -S parser -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=...`
5. Upload artifact `parser-win32-x64`：`parser/build/Release/codexray-parser.exe`

### 3.5 parser 二进制放置规则

`package-vsix` job 中，下载三份 artifact 后放置到：

```
resources/bin/
  darwin-arm64/
    codexray-parser
  linux-x64/
    codexray-parser
  win32-x64/
    codexray-parser.exe
```

`vsce package --target darwin-arm64` 时，`.vscodeignore` 控制只包含 `resources/bin/darwin-arm64/` 而排除其他平台目录。

### 3.6 .vscodeignore 调整策略

当前 `.vscodeignore` 需要修改：

- 排除 `parser/**`（源码不打包）
- 排除 `resources/bin/darwin-arm64/**`（其他平台不打包，通过 `--target` 参数结合条件脚本实现）
- 包含 `resources/bin/$TARGET/**`

**实现方式**：`package-vsix` job 中为每个 target 在打包前动态写 `.vscodeignore`，或者使用三个分别的 `package.json` target 配置。

推荐做法：在打包前执行 shell 脚本，将当前平台的 parser 二进制复制到固定路径（如 `resources/bin/codexray-parser` 或 `resources/bin/codexray-parser.exe`），`.vscodeignore` 只需排除其他目录。

---

## 4. 需要新增/修改的文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `.gitignore` | 修改 | 增加 `parser/third_party/llvm/` 等排除规则 |
| `.vscodeignore` | 修改 | 排除 `parser/` 源码，包含 `resources/bin/` |
| `package.json` | 修改 | `vscode:prepublish` 加入 UI build，增加 `build:ui` script |
| `.github/workflows/release.yml` | 新增 | 主 CI 工作流 |
| `parser/third_party/download_llvm.ps1` | 新增 | Windows 专用 LLVM 下载脚本 |
| `resources/bin/.gitkeep` | 新增 | 占位，确保 `resources/bin/` 被 git 追踪 |

---

## 5. 扩展侧 parser 路径适配

`src/` 中获取 parser 路径的逻辑（`getParserPath`）需要根据平台从 `resources/bin/` 读取：

```typescript
// 根据 process.platform + process.arch 决定 parser 二进制路径
function getParserPath(context: vscode.ExtensionContext): string {
  const platform = process.platform;  // 'darwin' | 'linux' | 'win32'
  const arch = process.arch;           // 'arm64' | 'x64'
  const target = `${platform}-${arch === 'arm64' ? 'arm64' : 'x64'}`;
  const binName = platform === 'win32' ? 'codexray-parser.exe' : 'codexray-parser';
  return context.asAbsolutePath(path.join('resources', 'bin', target, binName));
}
```

需要检查现有 `src/` 中的 parser 路径逻辑并统一适配。

---

## 6. 成功标准

1. 推送 `v0.1.0` tag 后，GitHub Actions 自动运行，三个 `build-parser` job 并行构建成功
2. `package-vsix` job 生成三个 `.vsix` 文件（`codexray-darwin-arm64-0.1.0.vsix` 等）
3. GitHub Release 页面自动创建，三个 `.vsix` 作为 Release Asset 上传
4. 每个 `.vsix` 安装到对应平台后，parser 二进制存在且可执行
5. 本地代码全部推送到 `git@github.com:yushun667/codeXray.git`
