#!/bin/bash
set -e

# 参考 sqlite3 模版
mkdir -p repo corpus/re2@ossfuzz

# 拉取 re2 源码
if [ ! -d repo/.git ]; then
  git clone https://github.com/google/re2.git repo --depth 1
else
  (cd repo && git pull --ff-only)
fi

# 从 oss-fuzz 拉取 fuzzer 源（re2_fuzzer.cc）
if [ ! -d oss-fuzz-temp ]; then
  git clone https://github.com/google/oss-fuzz.git oss-fuzz-temp --depth 1
else
  (cd oss-fuzz-temp && git pull --ff-only origin master || git pull --ff-only origin main || true)
fi

if [ -f oss-fuzz-temp/projects/re2/re2_fuzzer.cc ]; then
  cp -f oss-fuzz-temp/projects/re2/re2_fuzzer.cc repo/
fi

# 可选：复制 seeds（若存在）
if [ -d oss-fuzz-temp/projects/re2/seeds ]; then
  cp -rf oss-fuzz-temp/projects/re2/seeds/* corpus/re2@re2_fuzzer/ || true
fi

# 应用 MSan 抑制补丁
if [ -f repo.patch ]; then
  echo "应用 MSan 抑制补丁..."
  (cd repo && git apply ../repo.patch)
fi

# 从仓库中搜寻语料文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$SCRIPT_DIR/create_re2_corpus.sh" ]; then
    "$SCRIPT_DIR/create_re2_corpus.sh" || true
else
    echo "警告: 未找到可执行的 create_re2_corpus.sh，已跳过语料生成" >&2
fi  

rm -rf oss-fuzz-temp fuzzbench-temp

