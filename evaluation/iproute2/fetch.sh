#!/bin/bash
set -euo pipefail
export PROJ=iproute2
URL="https://github.com/iproute2/iproute2.git"
if [ ! -d repo/.git ]; then 
    git clone "$URL" repo --depth 1
else 
    echo "已存在 repo，跳过克隆"
fi

# 由于编译产物不需要提供输入，拉取相关说明文件，做为语料文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -x "$SCRIPT_DIR/create_iproute2_corpus.sh" ]; then
    "$SCRIPT_DIR/create_iproute2_corpus.sh" || true
else
    echo "警告: 未找到可执行的 create_iproute2_corpus.sh，已跳过语料生成" >&2
fi

