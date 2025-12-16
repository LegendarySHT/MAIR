#!/bin/bash
# MopIR 测试运行脚本

set -e

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_DIR="${SCRIPT_DIR}"  # 源代码目录

# 创建构建目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# 运行 CMake
echo "Running CMake..."
cmake ..

# 编译
echo "Building..."
make -j$(nproc)

# 运行测试
echo ""
echo "Running tests..."
echo "=================="

if [ -f "./MopIRTest" ]; then
    for ll in test_redundant.ll test_redundant_complex.ll test_recurrence.ll; do
        # 在源代码目录中查找 .ll 文件
        LL_FILE="${SOURCE_DIR}/$ll"
        if [ -f "$LL_FILE" ]; then
            echo "Running: $ll"
            ./MopIRTest "$LL_FILE"
            echo "------------------"
        else
            echo "Warning: $ll not found in ${SOURCE_DIR}."
        fi
    done
    echo "=================="
    echo "Tests completed!"
else
    echo "Error: MopIRTest executable not found!"
    exit 1
fi