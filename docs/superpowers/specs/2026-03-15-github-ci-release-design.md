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
resources/bin/**
!resources/bin/.gitkeep
```

> **注意**：用 `resources/bin/**` 而非 `resources/bin/` 排除目录内容，同时用 `!resources/bin/.gitkeep` 豁免占位文件，确保 `resources/bin/` 目录被 Git 追踪。

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

### 3.3 权限声明

`create-release` job 需要写入 Release，**在该 job 内部**声明（最小权限原则，不影响其他 job）：

```yaml
create-release:
  needs: package-vsix
  runs-on: ubuntu-22.04
  permissions:
    contents: write   # 仅此 job 需要，放在 job 级别而非 workflow 顶层
  steps: ...
```

### 3.4 Job 架构

```
build-parser (matrix: 3 platforms, 并行)
  ├─ macos-14        → artifact: parser-darwin-arm64
  ├─ ubuntu-22.04    → artifact: parser-linux-x64
  └─ windows-latest  → artifact: parser-win32-x64

package-vsix (needs: build-parser, runs-on: ubuntu-22.04)
  ├─ 下载三份 parser artifact → resources/bin/{target}/
  ├─ npm ci (root，含 @vscode/vsce)
  ├─ npm run compile (tsc)
  ├─ cd resources/ui && npm ci && npm run build
  ├─ 依次为每个 target：复制对应 parser 到固定路径 → vsce package --target
  └─ upload artifact: vsix-packages (3个 .vsix)

create-release (needs: package-vsix, runs-on: ubuntu-22.04)
  └─ gh release create $TAG 上传三个 .vsix
```

### 3.5 build-parser Job 细节

**matrix 定义**：

```yaml
strategy:
  matrix:
    include:
      - os: macos-14
        target: darwin-arm64
        llvm_suffix: aarch64-macos
      - os: ubuntu-22.04
        target: linux-x64
        llvm_suffix: x86_64-ubuntu-22.04
      - os: windows-latest
        target: win32-x64
        llvm_suffix: x86_64-windows
```

**LLVM 缓存**：使用 `actions/cache` 对 `parser/third_party/llvm` 目录缓存，key 为 `llvm-18.1.8-{matrix.llvm_suffix}`，避免每次重复下载约 400MB 包。

**macOS 步骤**：
1. `brew install protobuf nlohmann-json sqlite3`
2. `actions/cache` restore `parser/third_party/llvm`
3. `bash parser/third_party/download_llvm.sh`（已有脚本，若 llvm 目录已存在则自动跳过）
4. `cmake -B parser/build -S parser -DCMAKE_BUILD_TYPE=Release`
5. `cmake --build parser/build --target codexray-parser --config Release`
6. Upload artifact `parser-darwin-arm64`：`parser/build/codexray-parser`

**Linux 步骤**：
1. `sudo apt-get install -y libprotobuf-dev protobuf-compiler nlohmann-json3-dev libsqlite3-dev`
2. `actions/cache` restore + `bash parser/third_party/download_llvm.sh`
3. cmake 构建
4. Upload artifact `parser-linux-x64`：`parser/build/codexray-parser`

**Windows 步骤**：
1. `ilammy/msvc-dev-cmd` action 激活 VS 环境（仍是最佳实践）
2. `vcpkg install protobuf sqlite3 nlohmann-json` — 使用 `$env:VCPKG_INSTALLATION_ROOT`（GitHub runner 实际变量名，路径为 `C:\vcpkg`）
3. `actions/cache` restore `parser/third_party/llvm`
4. 执行 `parser/third_party/download_llvm.ps1` 下载解压（见下文）
5. cmake 构建：`cmake -B parser/build -S parser -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"`
6. `cmake --build parser/build --target codexray-parser --config Release`
7. Upload artifact `parser-win32-x64`：`parser/build/Release/codexray-parser.exe`

> **注意**：Windows runner 不要使用 `tar -xJf` 解压 `.xz`，必须用 7z（runner 已预装）。

### 3.6 download_llvm.ps1 脚本框架

新增 `parser/third_party/download_llvm.ps1`，与 `download_llvm.sh` 逻辑对应：

```powershell
# download_llvm.ps1 — Windows 专用 LLVM 下载脚本（x86_64-windows）
$ErrorActionPreference = "Stop"

$VERSION = "18.1.8"
$RELEASE_TAG = "v2025.3.0"
$LLVM_SUFFIX = "x86_64-windows"
$TARBALL = "clang+llvm-${VERSION}-${LLVM_SUFFIX}.tar.xz"
$URL = "https://github.com/awakecoding/llvm-prebuilt/releases/download/${RELEASE_TAG}/${TARBALL}"
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$DEST_DIR = Join-Path $SCRIPT_DIR "llvm"

if (Test-Path (Join-Path $DEST_DIR "lib\cmake\llvm\LLVMConfig.cmake")) {
    Write-Host "Already exists, skipping download."
    exit 0
}

$TARBALL_PATH = Join-Path $SCRIPT_DIR $TARBALL
if (-not (Test-Path $TARBALL_PATH)) {
    Write-Host "Downloading $URL ..."
    Invoke-WebRequest -Uri $URL -OutFile $TARBALL_PATH -UseBasicParsing
}

Write-Host "Extracting with 7z..."
$EXTRACT_DIR = Join-Path $SCRIPT_DIR ".tmp_llvm_extract"
if (Test-Path $EXTRACT_DIR) { Remove-Item $EXTRACT_DIR -Recurse -Force }
New-Item -ItemType Directory $EXTRACT_DIR | Out-Null

# 7z 分两步解压：.tar.xz → .tar → 目录
& "7z" x $TARBALL_PATH -o"$EXTRACT_DIR" -y
$TAR_FILE = Get-ChildItem $EXTRACT_DIR -Filter "*.tar" | Select-Object -First 1
& "7z" x $TAR_FILE.FullName -o"$EXTRACT_DIR" -y

$TOP = Get-ChildItem $EXTRACT_DIR -Directory | Where-Object { $_.Name -ne "" } | Select-Object -First 1
if ($null -eq $TOP) { Write-Error "Unexpected tarball layout"; exit 1 }
Move-Item $TOP.FullName $DEST_DIR
Remove-Item $EXTRACT_DIR -Recurse -Force

Write-Host "Done. LLVM at: $DEST_DIR"
```

### 3.7 parser 二进制放置规则

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

**打包时的固定路径策略**：为每个 target 打包前，将对应二进制复制到固定位置 `resources/bin/codexray-parser`（macOS/Linux）或 `resources/bin/codexray-parser.exe`（Windows），`.vscodeignore` 仅排除 `resources/bin/darwin-arm64/`、`resources/bin/linux-x64/`、`resources/bin/win32-x64/` 等子目录，只包含固定路径的二进制。

实际实现时用循环脚本完成，避免动态修改 `.vscodeignore`：

```bash
# 伪代码，在 package-vsix job 中执行三次
for target in darwin-arm64 linux-x64 win32-x64; do
  cp resources/bin/$target/codexray-parser* resources/bin/
  npx vsce package --target $target -o codexray-$target-$VERSION.vsix
  rm resources/bin/codexray-parser*
done
```

---

## 4. 需要新增/修改的文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `.gitignore` | 修改 | 增加 `parser/third_party/llvm/` 等排除规则，正确处理 `resources/bin/` |
| `.vscodeignore` | 修改 | 排除 `parser/`、`docs/`、`resources/ui/src/` 等源码目录；包含 `resources/bin/codexray-parser*` |
| `package.json` | 修改 | `vscode:prepublish` 只保留 `npm run compile`（tsc）；新增 `build:ui` script；将 `@vscode/vsce` 加入 `devDependencies` |
| `.github/workflows/release.yml` | 新增 | 主 CI 工作流 |
| `parser/third_party/download_llvm.ps1` | 新增 | Windows 专用 LLVM 下载脚本（使用 7z 解压） |
| `resources/bin/.gitkeep` | 新增 | 占位，让 Git 追踪 `resources/bin/` 目录 |
| `src/config.ts`（或相关文件） | 修改 | `getParserPath` 路径改为固定路径 `resources/bin/codexray-parser[.exe]`，与打包策略对齐 |

> **关于 `vscode:prepublish`**：CI 中手动先执行 `npm run compile` 和 `npm run build:ui`，`vscode:prepublish` 只执行 tsc 编译。`vsce package` 会调用 `vscode:prepublish`，但由于 tsc 是幂等的（产物已存在），不会重复构建 UI。

---

## 5. 扩展侧 parser 路径适配

`src/config.ts`（当前路径逻辑位于此文件）中的 `getParserPath` 需修改为**固定路径**，与 §3.7 打包策略对齐：

```typescript
// 修改前（旧路径）：
// return context.asAbsolutePath(path.join('bin', 'codexray-parser'));

// 修改后：固定路径，与打包时复制的位置一致
function getParserPath(context: vscode.ExtensionContext): string {
  const binName = process.platform === 'win32' ? 'codexray-parser.exe' : 'codexray-parser';
  return context.asAbsolutePath(path.join('resources', 'bin', binName));
}
```

> **路径统一策略**：打包时（§3.7）将各平台二进制复制到 `resources/bin/codexray-parser[.exe]`（无子目录），`getParserPath` 也指向同一位置。本地开发时，同样将构建好的 parser 放到 `resources/bin/codexray-parser[.exe]`，而非 `{target}` 子目录。`{target}` 子目录（`resources/bin/darwin-arm64/` 等）仅作为 CI 中的临时中转，不出现在运行时路径中。

---

## 6. .vscodeignore 最终状态

```
.vscode/**
.vscode-test/**
src/**
doc/**
docs/**
.git/**
.gitignore
tsconfig.json
**/*.map
**/*.ts
!resources/ui/dist/**/*.js
node_modules/**
!node_modules/@vscode/codicons/**
parser/**
resources/ui/src/**
resources/ui/index.html
resources/ui/tsconfig.json
resources/ui/vite.config.ts
resources/bin/darwin-arm64/**
resources/bin/linux-x64/**
resources/bin/win32-x64/**
```

> `resources/bin/codexray-parser` 和 `resources/bin/codexray-parser.exe`（固定路径复制产物）不被排除，从而被打包进 `.vsix`。各平台子目录被排除，避免打包不必要的二进制。

---

## 7. 成功标准

1. 本地所有新增/修改文件推送到 `git@github.com:yushun667/codeXray.git main` 分支
2. 推送 `v0.1.0` tag 后，GitHub Actions 自动运行
3. 三个 `build-parser` job 并行构建成功，各自 upload artifact
4. `package-vsix` job 生成三个 `.vsix`（`codexray-darwin-arm64-0.1.0.vsix` 等）
5. `create-release` job 创建 GitHub Release，三个 `.vsix` 作为 Asset 上传
6. 每个 `.vsix` 安装到对应平台后，`resources/bin/codexray-parser[.exe]` 存在且可执行
7. LLVM 包缓存生效，第二次 CI 运行构建时间明显缩短（下载阶段跳过）
