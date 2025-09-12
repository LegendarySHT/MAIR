#!/bin/bash

# -----------------------------------------------------------------------------
# 脚本说明：
#   本脚本 run.sh 为各基准程序提供统一的运行入口，自动调用 common-run.sh，
#   根据环境变量和参数配置，支持不同 Sanitizer 模式、输入文件、预/后置参数等。
#
# 用法（Usage）: 在 <package>/artefacts/<program>/run.sh 中调用
#   /path/to/this/run.sh <mode> [<input>]
#
#   <mode>   ：运行模式（如 raw, asan, msan, xsan-asan 等，需与编译时一致）
#   <input>  ：（可选）输入文件路径，若省略则由 common-run.sh 处理
#
# 环境变量要求：
#   PRE_OPT   ：（可为空）程序参数，放在输入文件前
#   POST_OPT  ：（可为空）程序参数，放在输入文件后
#   stdin     ：（必填）是否通过标准输入传递输入（true/false）
#   PROG_DIR  ：（必填）程序 artefacts 目录
#
# 示例：
#   PRE_OPT="-v" POST_OPT="--debug" stdin=false PROG_DIR=./artefacts/foo ./run.sh asan ./inputs/foo/input1
#   stdin=true PROG_DIR=./artefacts/bar ./run.sh xsan-asan ./inputs/bar/input2
# -----------------------------------------------------------------------------


# 通用 run 脚本

# 确保必要变量已定义(可以为空定义)
: "${PRE_OPT?需要定义 PRE_OPT}"
: "${POST_OPT?需要定义 POST_OPT}"
# 不能为空
: "${stdin:?需要定义 stdin}"
: "${PROG_DIR:?需要定义 PROG_DIR}"

usage() {
    echo "用法: $PROG_DIR/run.sh <mode> [<input>]"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

FILE_DIR=$(cd "$(dirname "$0")"; pwd)

mode="$1"
shift
input="${1:-}"
# 如果 input 为空，则从 corpus 目录获取默认输入文件
if [ -z "$input" ]; then
    # 提取 PROG_DIR 的 name
    prog_name=$(basename "$PROG_DIR")
    
    # 构建 corpus 目录路径
    corpus_dir="$PROG_DIR/../../corpus/$prog_name"
    
    # 检查 corpus 目录是否存在，并获取第一个文件
    if [ -d "$corpus_dir" ]; then
        # 获取目录下第一个文件
        first_file=$(find "$corpus_dir" -type f | head -n 1)
        if [ -n "$first_file" ]; then
            input="$first_file"
        fi
    fi
fi


"$FILE_DIR/common-run.sh" "$mode" "$PROG_DIR" "$stdin" "$input" "$PRE_OPT" "$POST_OPT"
