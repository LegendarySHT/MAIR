#!/bin/bash
set -e

# 参照 sqlite3 模板：从 git 拉源码与 fuzzer 文件
export PROJ="lcms"

# lcms2 源码仓库
SRC_URL="https://github.com/mm2/Little-CMS.git"

echo "克隆 lcms 源码..."
if [ ! -d "repo" ]; then
    git clone "$SRC_URL" repo --depth 1
else
    pushd repo >/dev/null
    git pull --ff-only || true
    popd >/dev/null
fi

# 从仓库中搜寻语料文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$SCRIPT_DIR/create_lcms_corpus.sh" ]; then
    # 若不可执行，尝试赋予执行权限；仍不可执行则用 bash 调用
    if [ ! -x "$SCRIPT_DIR/create_lcms_corpus.sh" ]; then
        chmod +x "$SCRIPT_DIR/create_lcms_corpus.sh" 2>/dev/null || true
    fi
    if [ -x "$SCRIPT_DIR/create_lcms_corpus.sh" ]; then
        "$SCRIPT_DIR/create_lcms_corpus.sh" || true
    else
        bash "$SCRIPT_DIR/create_lcms_corpus.sh" || true
    fi
else
    echo "警告: 未找到 create_lcms_corpus.sh，已跳过语料生成" >&2
fi

rm -rf oss-fuzz-temp fuzzbench-temp