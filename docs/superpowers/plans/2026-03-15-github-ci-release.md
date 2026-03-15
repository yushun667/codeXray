# GitHub CI Release Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 CodeXray 项目同步到 GitHub，并通过 GitHub Actions 在推送 `v*` tag 时自动为 macOS ARM64、Linux x86_64、Windows x64 三平台打包含 parser 二进制的 `.vsix` 插件。

**Architecture:** 三个并行的 `build-parser` job（每个平台各自下载 LLVM、cmake 构建 parser 二进制），完成后单个 `package-vsix` job 汇总三份产物并打包三个平台的 `.vsix`，最后 `create-release` job 创建 GitHub Release 并上传所有 `.vsix`。

**Tech Stack:** GitHub Actions, CMake 3.16+, MSVC + vcpkg (Windows), npm + @vscode/vsce, Vite (UI build), LLVM 18.1.8 预编译包 (awakecoding/llvm-prebuilt)

**Spec:** `docs/superpowers/specs/2026-03-15-github-ci-release-design.md`

---

## File Map

| 文件 | 操作 | 说明 |
|------|------|------|
| `.gitignore` | 修改 | 补充 parser/third_party/llvm/ 等排除规则，resources/bin/ 处理 |
| `.vscodeignore` | 修改 | 排除 parser/、docs/、resources/ui/src/ 等；保留 resources/bin/codexray-parser* |
| `package.json` | 修改 | vscode:prepublish 只含 tsc；新增 build:ui script；加入 @vscode/vsce devDep |
| `src/config.ts` | 修改 | getParserPath 改为 resources/bin/codexray-parser[.exe] 固定路径 |
| `resources/bin/.gitkeep` | 新增 | 占位文件，让 Git 追踪 resources/bin/ 目录 |
| `parser/third_party/download_llvm.ps1` | 新增 | Windows 专用 LLVM 下载脚本（PowerShell + 7z） |
| `.github/workflows/release.yml` | 新增 | 主 CI 工作流（触发/构建/打包/发布） |

---

## Chunk 1: 仓库准备——gitignore、vscodeignore、package.json、parser 路径

### Task 1: 更新 .gitignore

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: 查看当前 .gitignore 内容**

  ```bash
  cat .gitignore
  ```

- [ ] **Step 2: 追加缺失的排除规则**

  在 `.gitignore` 末尾追加（注意用 `resources/bin/**` + `!.gitkeep` 而非 `resources/bin/`）：

  ```
  parser/third_party/llvm/
  parser/third_party/clang/
  parser/third_party/lld/
  parser/third_party/*.tar.xz
  parser/build/
  resources/ui/node_modules/
  resources/ui/dist/
  resources/bin/**
  !resources/bin/.gitkeep
  docs/superpowers/
  .cursor/
  ```

  > 注：`docs/superpowers/` 排除规格文档和计划文档，避免打包进 vsix（但已在 vscodeignore 中处理，gitignore 可选）。

- [ ] **Step 3: 验证 git 不再追踪已排除路径**

  ```bash
  git status --short
  ```

  确认 `parser/third_party/llvm/`（若存在）不再出现在未追踪列表中。

- [ ] **Step 4: Commit**

  ```bash
  git add .gitignore
  git commit -m "chore: 更新 .gitignore，排除 LLVM/parser build/resources/bin 等"
  ```

---

### Task 2: 更新 .vscodeignore

**Files:**
- Modify: `.vscodeignore`

- [ ] **Step 1: 用新内容完整替换 .vscodeignore**

  将 `.vscodeignore` 内容替换为：

  ```
  # 开发工具配置
  .vscode/**
  .vscode-test/**
  .cursor/**
  .claude/**

  # 源码（TypeScript 原文件，只打包编译产物 out/）
  src/**
  tsconfig.json
  **/*.map

  # TypeScript 源文件（含 resources/ui/src）；注意 **/*.ts 不影响 .js 文件
  **/*.ts

  # 文档
  doc/**
  docs/**

  # Git
  .git/**
  .gitignore

  # Node 相关
  node_modules/**
  !node_modules/@vscode/codicons/**

  # Parser 源码（只打包二进制产物，不打包 C++ 源码）
  parser/**

  # UI 源文件（只打包 dist，不打包源码）
  resources/ui/src/**
  resources/ui/index.html
  resources/ui/tsconfig.json
  resources/ui/vite.config.ts
  resources/ui/package.json
  resources/ui/package-lock.json
  resources/ui/.gitignore
  resources/ui/node_modules/**

  # parser 二进制临时中转目录（打包时会将对应平台 parser 复制到固定路径）
  resources/bin/darwin-arm64/**
  resources/bin/linux-x64/**
  resources/bin/win32-x64/**
  ```

  > **说明**：`resources/bin/codexray-parser` 和 `resources/bin/codexray-parser.exe`（固定路径）不在排除列表中，会被打包进 `.vsix`。

- [ ] **Step 2: Commit**

  ```bash
  git add .vscodeignore
  git commit -m "chore: 更新 .vscodeignore，排除 parser 源码和 UI 源文件，包含 resources/bin 固定路径二进制"
  ```

---

### Task 3: 更新 package.json（vscode:prepublish + build:ui + @vscode/vsce）

**Files:**
- Modify: `package.json`

- [ ] **Step 1: 安装 @vscode/vsce 到 devDependencies**

  ```bash
  npm install --save-dev @vscode/vsce
  ```

- [ ] **Step 2: 在 package.json scripts 中添加 build:ui**

  当前 `vscode:prepublish` 已经是 `"npm run compile"`，**无需修改**。只需在 `scripts` 中新增 `build:ui` 条目：

  ```json
  "build:ui": "cd resources/ui && npm ci && npm run build"
  ```

  > **说明**：`vscode:prepublish` 已正确为 `npm run compile`（tsc 编译），不含 UI build。CI 中手动先执行 `build:ui`，再执行 `vsce package`（会触发 `vscode:prepublish` 即 tsc，幂等）。

- [ ] **Step 3: 验证 package.json 语法正确**

  ```bash
  node -e "require('./package.json')" && echo "OK"
  ```

- [ ] **Step 4: Commit**

  ```bash
  git add package.json package-lock.json
  git commit -m "chore: 添加 @vscode/vsce，规范 vscode:prepublish 和 build:ui script"
  ```

---

### Task 4: 修改 src/config.ts 的 getParserPath

**Files:**
- Modify: `src/config.ts:52-63`

- [ ] **Step 1: 修改 getParserPath 方法**

  将 `src/config.ts` 中的 `getParserPath` 方法替换为：

  ```typescript
  /**
   * 解析器可执行体路径
   * 打包版本：resources/bin/codexray-parser[.exe]（固定路径，由 CI 打包时复制进去）
   * 开发版本：同路径（本地开发时需手动将构建产物放置于此）
   * 若不存在则 fallback 到 parser/build/（仅供本地开发兜底）
   */
  getParserPath(): string {
    const base = process.platform === 'win32' ? 'codexray-parser.exe' : 'codexray-parser';
    if (!this._context) {
      return path.join('resources', 'bin', base);
    }
    const binPath = this._context.asAbsolutePath(path.join('resources', 'bin', base));
    if (fs.existsSync(binPath)) return binPath;
    // 本地开发兜底：parser/build/ 目录（cmake 构建产物）
    const buildPath = this._context.asAbsolutePath(path.join('parser', 'build', base));
    if (fs.existsSync(buildPath)) return buildPath;
    return binPath;
  }
  ```

- [ ] **Step 2: 编译验证无类型错误**

  ```bash
  npm run compile
  ```

  预期：无报错输出，`out/` 目录有更新。

- [ ] **Step 3: Commit**

  ```bash
  git add src/config.ts
  git commit -m "fix: getParserPath 改用 resources/bin/ 固定路径，与 CI 打包策略对齐"
  ```

---

### Task 5: 创建 resources/bin/.gitkeep

**Files:**
- Create: `resources/bin/.gitkeep`

- [ ] **Step 1: 创建目录和占位文件**

  ```bash
  mkdir -p resources/bin
  touch resources/bin/.gitkeep
  ```

- [ ] **Step 2: 确认 git 追踪到 .gitkeep 而不追踪其他内容**

  ```bash
  git status resources/bin/
  ```

  预期：看到 `?? resources/bin/.gitkeep`，且 `.gitignore` 中 `resources/bin/**` + `!resources/bin/.gitkeep` 使得只有 `.gitkeep` 被追踪。

- [ ] **Step 3: Commit**

  ```bash
  git add resources/bin/.gitkeep
  git commit -m "chore: 新增 resources/bin/.gitkeep，追踪 CI 存放 parser 二进制的目录"
  ```

---

## Chunk 2: Windows LLVM 下载脚本

### Task 6: 新增 parser/third_party/download_llvm.ps1

**Files:**
- Create: `parser/third_party/download_llvm.ps1`

- [ ] **Step 1: 创建 PowerShell 脚本**

  创建 `parser/third_party/download_llvm.ps1`，内容如下：

  ```powershell
  # download_llvm.ps1 — Windows 专用 LLVM+Clang 下载脚本
  # 使用 awakecoding/llvm-prebuilt releases（clang+llvm-18.1.8-x86_64-windows）
  # 依赖：7z（GitHub Windows runner 已预装），不使用 tar -xJf（Windows 兼容性问题）
  $ErrorActionPreference = "Stop"

  $VERSION      = "18.1.8"
  $RELEASE_TAG  = "v2025.3.0"
  $LLVM_SUFFIX  = "x86_64-windows"
  $TARBALL      = "clang+llvm-${VERSION}-${LLVM_SUFFIX}.tar.xz"
  $BASE_URL     = "https://github.com/awakecoding/llvm-prebuilt/releases/download/${RELEASE_TAG}"
  $URL          = "${BASE_URL}/${TARBALL}"

  $SCRIPT_DIR   = Split-Path -Parent $MyInvocation.MyCommand.Path
  $DEST_DIR     = Join-Path $SCRIPT_DIR "llvm"

  # 幂等性检查：若已存在则跳过
  if (Test-Path (Join-Path $DEST_DIR "lib\cmake\llvm\LLVMConfig.cmake")) {
      Write-Host "Already exists: $DEST_DIR (LLVMConfig.cmake present). Skip download."
      exit 0
  }

  $TARBALL_PATH = Join-Path $SCRIPT_DIR $TARBALL

  # 下载（若 tarball 已存在则跳过）
  if (-not (Test-Path $TARBALL_PATH)) {
      Write-Host "Downloading: $URL"
      Invoke-WebRequest -Uri $URL -OutFile $TARBALL_PATH -UseBasicParsing
  } else {
      Write-Host "Tarball already downloaded: $TARBALL_PATH"
  }

  # 解压：7z 分两步（.tar.xz → .tar → 目录）
  Write-Host "Extracting with 7z..."
  $EXTRACT_DIR = Join-Path $SCRIPT_DIR ".tmp_llvm_extract"
  if (Test-Path $EXTRACT_DIR) { Remove-Item $EXTRACT_DIR -Recurse -Force }
  New-Item -ItemType Directory $EXTRACT_DIR | Out-Null

  # 第一步：解压 .xz 得到 .tar
  & "7z" x $TARBALL_PATH "-o$EXTRACT_DIR" -y | Out-Null
  $TAR_FILE = Get-ChildItem $EXTRACT_DIR -Filter "*.tar" | Select-Object -First 1
  if ($null -eq $TAR_FILE) {
      Write-Error "No .tar file found after extracting .xz"
      exit 1
  }

  # 第二步：解压 .tar 得到目录
  & "7z" x $TAR_FILE.FullName "-o$EXTRACT_DIR" -y | Out-Null

  # 找到唯一顶级目录并移动到 llvm/
  $TOP = Get-ChildItem $EXTRACT_DIR -Directory | Where-Object { $_.Name -notlike "*.tar" } | Select-Object -First 1
  if ($null -eq $TOP) {
      Write-Error "Unexpected tarball layout (no top-level directory found)"
      exit 1
  }
  Move-Item $TOP.FullName $DEST_DIR
  Remove-Item $EXTRACT_DIR -Recurse -Force

  Write-Host "Done. LLVM is at: $DEST_DIR"
  if (Test-Path (Join-Path $DEST_DIR "lib\cmake\llvm\LLVMConfig.cmake")) {
      Write-Host "LLVMConfig.cmake found. CMake can use LLVM_DIR=$DEST_DIR\lib\cmake\llvm"
  } else {
      Write-Warning "LLVMConfig.cmake NOT found. Check tarball layout."
  }
  ```

- [ ] **Step 2: 验证脚本语法（本地 macOS/Linux 可跳过执行，检查格式即可）**

  ```bash
  # 检查文件存在且非空
  test -s parser/third_party/download_llvm.ps1 && echo "OK"
  ```

- [ ] **Step 3: Commit**

  ```bash
  git add parser/third_party/download_llvm.ps1
  git commit -m "feat: 新增 download_llvm.ps1，Windows CI 专用 LLVM 下载脚本（7z 解压）"
  ```

---

## Chunk 3: 提交并推送全部未追踪文件

### Task 7: 提交 resources/ui 新增文件

**Files:**
- Add: `resources/ui/src/**`、`resources/ui/index.html`、`resources/ui/package-lock.json`、`resources/ui/tsconfig.json`、`resources/ui/vite.config.ts`、`resources/ui/.gitignore`

- [ ] **Step 1: 查看 resources/ui 下未追踪的文件**

  ```bash
  git status resources/ui/
  ```

- [ ] **Step 2: 暂存并提交**

  ```bash
  git add resources/ui/
  git commit -m "feat: 新增 UI 源文件（main.ts/types.ts/graph/* 等 Vite+G6 v5 前端）"
  ```

---

### Task 8: 提交 doc/ 新增文档和其他修改文件

**Files:**
- Add: `doc/` 新增文档、`.vscode/launch.json`、`.vscode/tasks.json` 等

- [ ] **Step 1: 查看当前所有未提交变更**

  ```bash
  git status --short
  ```

  > **注意**：`parser/src/`、`parser/CMakeLists.txt`、`.vscode/launch.json` 等文件在之前的提交中已经包含在 HEAD 中，不需要再次 add。只需处理实际 `??` 状态的未追踪文件（如 `doc/` 下的新文档）。

- [ ] **Step 2: 同步远程变更（先 rebase，避免 push 被拒）**

  ```bash
  git pull --rebase origin main
  ```

  预期：rebase 成功，无冲突（本地变更只有 `docs/superpowers/` 目录，与远程 parser 修复提交不重叠）。

- [ ] **Step 3: 暂存 doc/ 下的新增文档（如有）**

  ```bash
  git status --short
  git add doc/
  ```

  > 若 `doc/` 没有未追踪文件，此步骤为空操作，跳过。

- [ ] **Step 4: 提交（若有新增内容）**

  ```bash
  # 仅在 Step 3 有实际暂存内容时执行
  git diff --cached --quiet || git commit -m "chore: 提交 doc 文档更新"
  ```

- [ ] **Step 5: 推送到 GitHub**

  ```bash
  git push origin main
  ```

  预期：推送成功，无 rejected 错误（已在 Step 2 完成 rebase）。

---

## Chunk 4: GitHub Actions 工作流

### Task 9: 新增 .github/workflows/release.yml

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: 创建 .github/workflows/ 目录**

  ```bash
  mkdir -p .github/workflows
  ```

- [ ] **Step 2: 创建 release.yml**

  创建 `.github/workflows/release.yml`，完整内容如下：

  ```yaml
  # CodeXray 三平台打包发布工作流
  # 触发：推送 v* tag（如 v0.1.0）
  # 产物：codexray-darwin-arm64-x.y.z.vsix / codexray-linux-x64-x.y.z.vsix / codexray-win32-x64-x.y.z.vsix
  name: Release

  on:
    push:
      tags:
        - 'v*'

  jobs:
    # ─────────────────────────────────────────────────────────────────────────────
    # Job 1: 三平台并行构建 C++ parser 二进制
    # ─────────────────────────────────────────────────────────────────────────────
    build-parser:
      name: Build Parser (${{ matrix.target }})
      strategy:
        fail-fast: false
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

      runs-on: ${{ matrix.os }}

      steps:
        - name: Checkout
          uses: actions/checkout@v4

        # ── macOS 依赖 ──────────────────────────────────────────────────────────
        - name: Install dependencies (macOS)
          if: runner.os == 'macOS'
          run: brew install protobuf nlohmann-json sqlite3

        # ── Linux 依赖 ──────────────────────────────────────────────────────────
        - name: Install dependencies (Linux)
          if: runner.os == 'Linux'
          run: |
            sudo apt-get update -q
            sudo apt-get install -y libprotobuf-dev protobuf-compiler nlohmann-json3-dev libsqlite3-dev

        # ── Windows 依赖（vcpkg） ────────────────────────────────────────────────
        - name: Install dependencies (Windows)
          if: runner.os == 'Windows'
          run: |
            vcpkg install protobuf sqlite3 nlohmann-json --triplet x64-windows

        # ── LLVM 缓存（所有平台）────────────────────────────────────────────────
        - name: Cache LLVM
          id: cache-llvm
          uses: actions/cache@v4
          with:
            path: parser/third_party/llvm
            key: llvm-18.1.8-${{ matrix.llvm_suffix }}

        # ── 下载 LLVM（macOS / Linux）─────────────────────────────────────────
        - name: Download LLVM (macOS/Linux)
          if: runner.os != 'Windows' && steps.cache-llvm.outputs.cache-hit != 'true'
          run: bash parser/third_party/download_llvm.sh

        # ── 下载 LLVM（Windows）──────────────────────────────────────────────
        - name: Download LLVM (Windows)
          if: runner.os == 'Windows' && steps.cache-llvm.outputs.cache-hit != 'true'
          shell: pwsh
          run: parser/third_party/download_llvm.ps1

        # ── 激活 MSVC 环境（Windows）─────────────────────────────────────────
        - name: Setup MSVC (Windows)
          if: runner.os == 'Windows'
          uses: ilammy/msvc-dev-cmd@v1

        # ── CMake 构建（macOS / Linux）────────────────────────────────────────
        - name: Build parser (macOS/Linux)
          if: runner.os != 'Windows'
          run: |
            cmake -B parser/build -S parser -DCMAKE_BUILD_TYPE=Release
            cmake --build parser/build --target codexray-parser --config Release

        # ── CMake 构建（Windows）─────────────────────────────────────────────
        - name: Build parser (Windows)
          if: runner.os == 'Windows'
          run: |
            cmake -B parser/build -S parser -DCMAKE_BUILD_TYPE=Release `
              -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
            cmake --build parser/build --target codexray-parser --config Release

        # ── Upload parser 二进制（macOS / Linux）─────────────────────────────
        - name: Upload parser artifact (macOS/Linux)
          if: runner.os != 'Windows'
          uses: actions/upload-artifact@v4
          with:
            name: parser-${{ matrix.target }}
            path: parser/build/codexray-parser
            if-no-files-found: error

        # ── Upload parser 二进制（Windows）────────────────────────────────────
        - name: Upload parser artifact (Windows)
          if: runner.os == 'Windows'
          uses: actions/upload-artifact@v4
          with:
            name: parser-${{ matrix.target }}
            path: parser/build/Release/codexray-parser.exe
            if-no-files-found: error

    # ─────────────────────────────────────────────────────────────────────────────
    # Job 2: 汇总三份 parser 产物，打包三个平台 .vsix
    # ─────────────────────────────────────────────────────────────────────────────
    package-vsix:
      name: Package VSIX
      needs: build-parser
      runs-on: ubuntu-22.04

      steps:
        - name: Checkout
          uses: actions/checkout@v4

        # ── 下载三份 parser artifact ────────────────────────────────────────────
        - name: Download parser (darwin-arm64)
          uses: actions/download-artifact@v4
          with:
            name: parser-darwin-arm64
            path: resources/bin/darwin-arm64

        - name: Download parser (linux-x64)
          uses: actions/download-artifact@v4
          with:
            name: parser-linux-x64
            path: resources/bin/linux-x64

        - name: Download parser (win32-x64)
          uses: actions/download-artifact@v4
          with:
            name: parser-win32-x64
            path: resources/bin/win32-x64

        # 设置可执行权限（macOS/Linux 二进制）
        - name: Set executable permissions
          run: |
            chmod +x resources/bin/darwin-arm64/codexray-parser
            chmod +x resources/bin/linux-x64/codexray-parser

        # ── 安装依赖 + 编译 TypeScript ──────────────────────────────────────────
        - name: Setup Node.js
          uses: actions/setup-node@v4
          with:
            node-version: '20'

        - name: Install root dependencies
          run: npm ci

        - name: Compile TypeScript
          run: npm run compile

        # ── 构建 UI ─────────────────────────────────────────────────────────────
        - name: Build UI
          run: npm run build:ui

        # ── 获取版本号（从 tag）────────────────────────────────────────────────
        - name: Get version from tag
          id: version
          run: echo "VERSION=${GITHUB_REF_NAME#v}" >> $GITHUB_OUTPUT

        # ── 打包三个平台 .vsix ──────────────────────────────────────────────────
        # 每次打包前将对应平台 parser 复制到固定路径，打包完成后删除
        - name: Package darwin-arm64
          run: |
            cp resources/bin/darwin-arm64/codexray-parser resources/bin/codexray-parser
            npx vsce package --target darwin-arm64 -o codexray-darwin-arm64-${{ steps.version.outputs.VERSION }}.vsix
            rm resources/bin/codexray-parser

        - name: Package linux-x64
          run: |
            cp resources/bin/linux-x64/codexray-parser resources/bin/codexray-parser
            npx vsce package --target linux-x64 -o codexray-linux-x64-${{ steps.version.outputs.VERSION }}.vsix
            rm resources/bin/codexray-parser

        - name: Package win32-x64
          run: |
            cp resources/bin/win32-x64/codexray-parser.exe resources/bin/codexray-parser.exe
            npx vsce package --target win32-x64 -o codexray-win32-x64-${{ steps.version.outputs.VERSION }}.vsix
            rm resources/bin/codexray-parser.exe

        # ── Upload .vsix artifacts ──────────────────────────────────────────────
        - name: Upload VSIX artifacts
          uses: actions/upload-artifact@v4
          with:
            name: vsix-packages
            path: '*.vsix'
            if-no-files-found: error

    # ─────────────────────────────────────────────────────────────────────────────
    # Job 3: 创建 GitHub Release 并上传 .vsix
    # ─────────────────────────────────────────────────────────────────────────────
    create-release:
      name: Create Release
      needs: package-vsix
      runs-on: ubuntu-22.04
      permissions:
        contents: write  # 仅此 job 需要写权限，最小权限原则

      steps:
        - name: Get version from tag
          id: version
          run: echo "VERSION=${GITHUB_REF_NAME#v}" >> $GITHUB_OUTPUT

        - name: Download VSIX artifacts
          uses: actions/download-artifact@v4
          with:
            name: vsix-packages
            path: vsix-packages

        - name: Create GitHub Release
          uses: softprops/action-gh-release@v2
          with:
            tag_name: ${{ github.ref_name }}
            name: "CodeXray v${{ steps.version.outputs.VERSION }}"
            body: |
              ## CodeXray v${{ steps.version.outputs.VERSION }}

              ### 安装方式

              根据平台下载对应 `.vsix` 文件，在 VSCode 中通过 "Extensions: Install from VSIX..." 安装。

              | 平台 | 文件 |
              |------|------|
              | macOS ARM64 (Apple Silicon) | `codexray-darwin-arm64-${{ steps.version.outputs.VERSION }}.vsix` |
              | Linux x86_64 | `codexray-linux-x64-${{ steps.version.outputs.VERSION }}.vsix` |
              | Windows x64 | `codexray-win32-x64-${{ steps.version.outputs.VERSION }}.vsix` |
            draft: false
            prerelease: false
            files: vsix-packages/*.vsix
  ```

- [ ] **Step 3: 验证 YAML 语法**

  ```bash
  # 若安装了 python-yaml 或 yq 可用以下命令，否则跳过到 Step 4
  python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml'))" && echo "YAML OK"
  ```

- [ ] **Step 4: Commit**

  ```bash
  git add .github/workflows/release.yml
  git commit -m "ci: 新增 GitHub Actions 三平台打包发布工作流（release.yml）"
  ```

- [ ] **Step 5: 推送到 GitHub**

  ```bash
  git push origin main
  ```

---

## Chunk 5: 验证与发布

### Task 10: 本地验证（可选，确认打包逻辑正确）

**Files:** 无新增文件

- [ ] **Step 1: 本地构建 UI**

  ```bash
  npm run build:ui
  ```

  预期：`resources/ui/dist/` 下出现 `graph.html` 和 `assets/` 目录。

- [ ] **Step 2: 本地编译 TypeScript**

  ```bash
  npm run compile
  ```

  预期：`out/` 目录有更新，无错误。

- [ ] **Step 3: （可选）本地试打包验证 .vscodeignore**

  前提：先将本地构建的 parser 放到 `resources/bin/codexray-parser`（macOS）。

  ```bash
  # 若本地有 parser 二进制
  cp parser/build/codexray-parser resources/bin/codexray-parser 2>/dev/null || echo "no local parser, skip"
  npx vsce package --target darwin-arm64 -o /tmp/codexray-test.vsix
  # 正向验证（应存在）
  echo "=== 正向验证（应存在）==="
  unzip -l /tmp/codexray-test.vsix | grep -E "bin/codexray-parser|ui/dist"
  # 反向验证（不应存在）
  echo "=== 反向验证（不应存在 C++ 源码和 TS 源码）==="
  unzip -l /tmp/codexray-test.vsix | grep "extension/parser/src/" && echo "FAIL: parser src found" || echo "OK: no parser src"
  unzip -l /tmp/codexray-test.vsix | grep "resources/ui/src/" && echo "FAIL: ui src found" || echo "OK: no ui src"
  ```

  预期：
  - `extension/resources/bin/codexray-parser` 存在
  - `extension/resources/ui/dist/graph.html` 存在
  - 无 `extension/parser/src/` 等 C++ 源码
  - 无 `extension/resources/ui/src/` 等 TypeScript 源码

---

### Task 11: 触发 GitHub Actions 打包

- [ ] **Step 1: 确认所有更改已推送**

  ```bash
  git log --oneline origin/main..HEAD
  ```

  预期：无输出（本地与远程同步）。

- [ ] **Step 2: 创建并推送 tag**

  ```bash
  git tag v0.1.0
  git push origin v0.1.0
  ```

- [ ] **Step 3: 在 GitHub 上查看 Actions 运行状态**

  访问：`https://github.com/yushun667/codeXray/actions`

  观察：
  1. `build-parser` 三个 job 并行运行
  2. `package-vsix` job 在三者完成后启动
  3. `create-release` job 最后运行

- [ ] **Step 4: 验证 GitHub Release 页面**

  访问：`https://github.com/yushun667/codeXray/releases`

  预期：自动创建 `v0.1.0` Release，包含三个 `.vsix` 文件作为 Asset。

---

## 成功标准（验收）

- [ ] 所有本地修改（`resources/ui/src/`、`doc/`、`parser/src/` 等）已推送到 `origin main`
- [ ] GitHub Actions `release.yml` 工作流存在于仓库中
- [ ] 推送 `v0.1.0` tag 后三平台 `build-parser` job 全部成功
- [ ] 生成三个 `.vsix` 文件：`codexray-darwin-arm64-0.1.0.vsix`、`codexray-linux-x64-0.1.0.vsix`、`codexray-win32-x64-0.1.0.vsix`
- [ ] GitHub Release 页面显示三个 `.vsix` 作为 Asset
- [ ] 打包内容验证：`.vsix` 中包含 `resources/bin/codexray-parser[.exe]` 和 `resources/ui/dist/graph.html`，不含 `parser/src/` 或 `resources/ui/src/`
