#!/bin/bash
# MopIR 测试运行脚本

set -e

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/build"

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
    ./MopIRTest test_simple.ll
    echo ""
    echo "=================="
    echo "Tests completed!"
else
    echo "Error: MopIRTest executable not found!"
    exit 1
fi
