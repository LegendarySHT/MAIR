#!/bin/bash
set -e

mkdir -p repo artefacts/lua corpus/lua temp/.bin

if [ ! -d repo/.git ]; then
  git clone https://github.com/lua/lua.git repo --depth 1 --branch master
else
  (cd repo && git pull --ff-only)
fi


# 仓库没有提供测试文件，手动生成测试文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$SCRIPT_DIR/create_lua_corpus.sh" ]; then
    "$SCRIPT_DIR/create_lua_corpus.sh" || true
else
    echo "警告: 未找到可执行的 create_lua_corpus.sh，已跳过语料生成" >&2
fi



