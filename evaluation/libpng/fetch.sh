#!/bin/bash
set -e

# 与 sqlite3/libjpeg-turbo 一致：使用 git 拉取源码，并从 fuzzbench 拉取 fuzzer 相关文件

export PROJ=libpng
URL="https://github.com/glennrp/libpng.git"

rm -rf repo
git clone "$URL" repo --depth 1

echo "从 fuzzbench 拉取 fuzzer 文件..."
if [ ! -d "fuzzbench-temp" ]; then
  git clone https://github.com/google/fuzzbench.git fuzzbench-temp --depth 1
else
  pushd fuzzbench-temp
  git pull --ff-only origin master || git pull --ff-only origin main || true
  popd
fi

# 从 fuzzbench 拷贝元数据（基准名称：libpng_libpng_read_fuzzer），其本身不包含 .cc 源
if [ -f "fuzzbench-temp/benchmarks/libpng_read_fuzzer/build.sh" ]; then
  cp "fuzzbench-temp/benchmarks/libpng_read_fuzzer/build.sh" repo/
fi
if [ -f "fuzzbench-temp/benchmarks/libpng_read_fuzzer/benchmark.yaml" ]; then
  cp "fuzzbench-temp/benchmarks/libpng_read_fuzzer/benchmark.yaml" repo/
fi

# 清理临时目录
rm -rf fuzzbench-temp

# 补充：从 oss-fuzz 拉取真正的 fuzzer 源文件 libpng_read_fuzzer.cc
echo "从 oss-fuzz 拉取 libpng_read_fuzzer.cc..."
if [ ! -d "oss-fuzz-temp" ]; then
  git clone https://github.com/google/oss-fuzz.git oss-fuzz-temp --depth 1
else
  pushd oss-fuzz-temp
  git pull --ff-only origin master || git pull --ff-only origin main || true
  popd
fi

if [ -f "oss-fuzz-temp/projects/libpng/libpng_read_fuzzer.cc" ]; then
  cp "oss-fuzz-temp/projects/libpng/libpng_read_fuzzer.cc" repo/
else
  echo "警告: 未在 oss-fuzz 中找到 libpng_read_fuzzer.cc" >&2
fi

rm -rf oss-fuzz-temp

echo "fetch 完成。"

#!/bin/bash
set -e

# 从fuzzbench复制源码
echo "从fuzzbench复制 libpng 源码..."
cp -r "/data/fuzz/fuzzbench/benchmarks/libpng_libpng_read_fuzzer"/* repo/
echo "完成: libpng 源码获取"

# 从仓库中搜寻语料文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$SCRIPT_DIR/create_libpng_corpus.sh" ]; then
    "$SCRIPT_DIR/create_libpng_corpus.sh" || true
else
    echo "警告: 未找到可执行的 create_libpng_corpus.sh，已跳过语料生成" >&2
fi
