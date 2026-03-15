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
