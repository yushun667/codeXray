#!/usr/bin/env bash
# 将 LLVM+Clang 预编译包下载并解压到 parser/third_party/llvm
# 使用 awakecoding/llvm-prebuilt releases（clang+llvm-18.1.8）

set -e

VERSION="18.1.8"
RELEASE_TAG="v2025.3.0"
BASE_URL="https://github.com/awakecoding/llvm-prebuilt/releases/download/${RELEASE_TAG}"

# 脚本所在目录与 parser 根目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="${SCRIPT_DIR}"
PARSER_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEST_DIR="${THIRD_PARTY_DIR}/llvm"

OS="$(uname -s)"
ARCH="$(uname -m)"

case "${OS}" in
  Darwin)  PLATFORM="macos" ;;
  Linux)   PLATFORM="ubuntu-22.04" ;;
  *)
    echo "Unsupported OS: ${OS}" >&2
    exit 1
    ;;
esac

case "${ARCH}" in
  arm64|aarch64)  ARCH_NAME="aarch64" ;;
  x86_64)         ARCH_NAME="x86_64" ;;
  *)
    echo "Unsupported arch: ${ARCH}" >&2
    exit 1
    ;;
esac

if [ "${PLATFORM}" = "macos" ]; then
  PKG_SUFFIX="${ARCH_NAME}-macos"
else
  PKG_SUFFIX="${ARCH_NAME}-${PLATFORM}"
fi

TARBALL="clang+llvm-${VERSION}-${PKG_SUFFIX}.tar.xz"
URL="${BASE_URL}/${TARBALL}"

echo "Using: ${TARBALL}"
echo "URL:   ${URL}"
echo "Dest:  ${DEST_DIR}"

if [ -d "${DEST_DIR}/lib/cmake/llvm" ]; then
  echo "Already exists: ${DEST_DIR} (lib/cmake/llvm present). Skip download."
  exit 0
fi

mkdir -p "${THIRD_PARTY_DIR}"
cd "${THIRD_PARTY_DIR}"

if [ ! -f "${TARBALL}" ]; then
  echo "Downloading..."
  curl -sL -o "${TARBALL}" "${URL}"
fi

echo "Extracting..."
rm -rf "${DEST_DIR}"
# 解压后通常得到单个顶层目录，先解压到临时目录再移动
EXTRACT_DIR=".tmp_llvm_extract"
rm -rf "${EXTRACT_DIR}"
mkdir -p "${EXTRACT_DIR}"
tar -xJf "${TARBALL}" -C "${EXTRACT_DIR}"

# 将唯一的顶层目录重命名为 llvm
TOP="$(ls -1 "${EXTRACT_DIR}")"
if [ "$(echo "${TOP}" | wc -l)" -ne 1 ]; then
  echo "Unexpected tarball layout (multiple top-level dirs)." >&2
  exit 1
fi
mv "${EXTRACT_DIR}/${TOP}" "${DEST_DIR}"
rmdir "${EXTRACT_DIR}"

# 可选：删除 tarball 以节省空间
# rm -f "${TARBALL}"

echo "Done. LLVM is at: ${DEST_DIR}"
if [ -f "${DEST_DIR}/lib/cmake/llvm/LLVMConfig.cmake" ]; then
  echo "LLVMConfig.cmake found. CMake can use LLVM_DIR=${DEST_DIR}/lib/cmake/llvm"
fi
