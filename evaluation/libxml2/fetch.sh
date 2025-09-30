#!/bin/bash
set -e

# 创建必要的目录
mkdir -p repo corpus/libxml2@ossfuzz

# 从 libxml2 官方仓库克隆源码
if [ ! -d "repo/.git" ]; then
    git clone https://gitlab.gnome.org/GNOME/libxml2.git repo
else
    pushd repo
    git pull origin master
    popd
fi

# 从 oss-fuzz 拉取 fuzzer 文件
echo "从 oss-fuzz 拉取 fuzzer 文件..."
if [ ! -d "oss-fuzz-temp" ]; then
    git clone https://github.com/google/oss-fuzz.git oss-fuzz-temp --depth 1
else
    pushd oss-fuzz-temp
    git pull origin master || git pull origin main
    popd
fi

# 从 fuzzbench 拉取 fuzzer 文件
echo "从 fuzzbench 拉取 fuzzer 文件..."
if [ ! -d "fuzzbench-temp" ]; then
    git clone https://github.com/google/fuzzbench.git fuzzbench-temp --depth 1
else
    pushd fuzzbench-temp
    git pull origin master || git pull origin main
    popd
fi

# 检查 oss-fuzz 中是否有 libxml2 项目
echo "检查 oss-fuzz 中的 libxml2 项目..."
if [ -d "oss-fuzz-temp/projects" ]; then
    ls oss-fuzz-temp/projects/ | grep -i xml || echo "未找到 XML 相关项目"
fi

# 复制 fuzzer 源文件
if [ -f "oss-fuzz-temp/projects/libxml2/xml_read_memory_fuzzer.c" ]; then
    cp oss-fuzz-temp/projects/libxml2/xml_read_memory_fuzzer.c repo/
    echo "已复制 xml_read_memory_fuzzer.c"
else
    echo "警告: 未找到 xml_read_memory_fuzzer.c"
fi

if [ -f "oss-fuzz-temp/projects/libxml2/xml_read_memory_fuzzer.h" ]; then
    cp oss-fuzz-temp/projects/libxml2/xml_read_memory_fuzzer.h repo/
    echo "已复制 xml_read_memory_fuzzer.h"
else
    echo "警告: 未找到 xml_read_memory_fuzzer.h"
fi

# 跳过 autogen.sh，直接使用预生成的 configure 脚本
echo "跳过 autogen.sh，使用预生成的 configure 脚本..."

echo "fetch 完成。"


# 从仓库中搜寻语料文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$SCRIPT_DIR/create_libxml2_corpus.sh" ]; then
    "$SCRIPT_DIR/create_libxml2_corpus.sh" || true
else
    echo "警告: 未找到可执行的 create_libxml2_corpus.sh，已跳过语料生成" >&2
fi

rm -rf oss-fuzz-temp fuzzbench-temp