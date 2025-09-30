#!/bin/bash
# 创建 xz 测试语料：
# - 复制 binutils/corpus/nm-new/objdump.raw 为原始种子
# - 使用 xz 压缩得到 objdump.raw.xz

set -euo pipefail

SRC="../binutils/corpus/nm-new/objdump.raw"
DEST_DIR="corpus/xz"
SEED_RAW="${DEST_DIR}/objdump.raw"
SEED_XZ="${DEST_DIR}/objdump.raw.xz"
XZ_BIN="./artefacts/xz/xz.raw"

# 先清空目标目录
if [ -d "${DEST_DIR}" ]; then
  rm -rf "${DEST_DIR:?}"/*
else
  mkdir -p "${DEST_DIR}"
fi

# 确保目录存在
mkdir -p "${DEST_DIR}"

if [ ! -f "${SRC}" ]; then
  echo "ERROR: 找不到源文件: ${SRC}" >&2
  exit 1
fi

if [ ! -x "${XZ_BIN}" ]; then
  echo "ERROR: 找不到 xz 可执行文件: ${XZ_BIN} (请先编译 xz)" >&2
  exit 1
fi

echo "复制原始种子..."
cp -f "${SRC}" "${SEED_RAW}"

echo "创建压缩种子 (.xz)..."
"${XZ_BIN}" -c "${SEED_RAW}" > "${SEED_XZ}"

echo "语料准备完成！"
ls -la "${DEST_DIR}"
echo "文件数量: $(ls "${DEST_DIR}" | wc -l)"
echo "总大小: $(du -sh "${DEST_DIR}")"






