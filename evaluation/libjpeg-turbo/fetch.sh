#!/bin/bash
export PROJ=libjpeg-turbo
URL="https://github.com/libjpeg-turbo/libjpeg-turbo.git"

# Use depth 1 to avoid cloning the history
git clone $URL repo --depth 1

# 从 fuzzbench git 拉取 fuzzer 代码
echo "从 fuzzbench git 拉取 fuzzer 代码..."
if [ ! -d "fuzzbench-temp" ]; then
    git clone https://github.com/google/fuzzbench.git fuzzbench-temp --depth 1
else
    pushd fuzzbench-temp
    git pull origin main
    popd
fi

# 复制 fuzzer 相关文件到 repo
cp "fuzzbench-temp/benchmarks/libjpeg-turbo_libjpeg_turbo_fuzzer/libjpeg_turbo_fuzzer.cc" repo/
cp "fuzzbench-temp/benchmarks/libjpeg-turbo_libjpeg_turbo_fuzzer/build.sh" repo/
cp "fuzzbench-temp/benchmarks/libjpeg-turbo_libjpeg_turbo_fuzzer/benchmark.yaml" repo/

# 清理临时目录
rm -rf fuzzbench-temp

# 从仓库中搜寻语料文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$SCRIPT_DIR/create_libjpeg-turbo_corpus.sh" ]; then
    "$SCRIPT_DIR/create_libjpeg-turbo_corpus.sh" || true
else
    echo "警告: 未找到可执行的 create_libjpeg-turbo_corpus.sh，已跳过语料生成" >&2
fi

