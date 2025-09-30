#!/bin/bash
export PROJ=xz
URL="https://github.com/tukaani-project/xz.git"

# Use depth 1 to avoid cloning the history
git clone "$URL" repo --depth 1

#创建xz的语料文件
mkdir -p corpus/xz
cp repo/testdata/xz* corpus/xz

# 从仓库中搜寻语料文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$SCRIPT_DIR/create_xz_corpus.sh" ]; then
    "$SCRIPT_DIR/create_xz_corpus.sh" || true
else
    echo "警告: 未找到可执行的 create_xz_corpus.sh，已跳过语料生成" >&2
fi  



