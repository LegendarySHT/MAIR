#!/bin/bash

# -----------------------------------------------------------------------------
# 脚本说明：
#   本脚本 common-run.sh 用于统一在不同 Sanitizer 配置下运行指定的基准程序。
#   支持通过参数灵活指定运行模式、程序目录、输入方式（标准输入或文件）、以及额外参数。
#
# Usage:
#   ./common-run.sh <mode> <program-dir> <stdin:bool> [<input>] [preoptions] [postoptions]
#
# 参数说明：
#   <mode>           : 运行模式（如 asan, msan, xsan-asan 等，需与编译时一致）
#   <program-dir>    : 程序 artefacts 所在目录（如 ./artefacts/foo）
#   <stdin:bool>     : 是否通过标准输入传递输入（true/false）
#   <input>          : （可选）输入文件路径
#   preoptions       : （可选）程序参数，放在输入文件前
#   postoptions      : （可选）程序参数，放在输入文件后
#
# 示例：
#   ./common-run.sh asan ./artefacts/foo false ./inputs/foo/input1 -v
#   ./common-run.sh xsan-asan ./artefacts/bar true ./inputs/bar/input2
#
# 典型用途：
#   供 /run.sh 调用，用于统一在不同 Sanitizer 配置下运行指定的基准程序。
# -----------------------------------------------------------------------------


# 用法说明
usage() {
    echo "用法: $0 <mode> <program-dir> <stdin:bool> [<input>] [preoptions] [postoptions]"
    echo "  <mode>           : 运行模式（如 asan, msan, xsan-asan 等）"
    echo "  <program-dir>    : 程序所在目录"
    echo "  <use-perf:bool>  : 是否进行CPU + Memory 性能测量（true/false）"
    echo "  <stdin:bool>     : 是否通过标准输入传递输入（true/false）"
    echo "  <input>          : （可选）输入文件路径"
    echo "  preoptions       : （可选）程序参数，放在输入文件前"
    echo "  postoptions      : （可选）程序参数，放在输入文件后"
    echo "示例:"
    echo "  $0 asan ./artefacts/foo false ./inputs/foo/input1 -v"
    echo "  $0 xsan-asan ./artefacts/bar true ./inputs/bar/input2"
    exit 1
}

# 检查参数数量
if [ $# -lt 3 ]; then
    echo "参数数量不足。"
    usage
fi

export ASAN_OPTIONS=detect_leaks=0
export TSAN_OPTIONS=report_bugs=0

SCRIPTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)
PERF_SCRIPT=$SCRIPTS_DIR/eval-sample.sh

mode="$1"
program_path="$2"/$(basename "$2")
use_perf="$3"
stdin="$4"
shift 4

# 判断 stdin 是否为 true/false
if [[ "$stdin" != "true" && "$stdin" != "false" ]]; then
    echo "<stdin:bool> 参数必须为 true 或 false。"
    usage
fi

# 判断程序文件是否存在
prog_bin="${program_path}.${mode}"
if [ ! -f "$prog_bin" ]; then
    echo "错误: 程序文件 $prog_bin 不存在。"
    exit 2
fi

# 处理 input 参数
input=""
preoptions=""
postoptions=""
if [ $# -ge 3 ]; then
    input="$1"
    preoptions="$2"
    postoptions="$3"
    shift 3
elif [ $# -ge 2 ]; then
    input="$1"
    preoptions="$2"
    shift 2
elif [ $# -ge 1 ]; then
    input="$1"
    shift 1
fi

# 如果需要输入文件但未提供
if [ -n "$input" ] && [ ! -f "$input" ]; then
    echo "错误: 输入文件 $input 不存在。"
    exit 3
fi


if [ "$stdin" = "false" ]; then
    # 如果 input 存在，插入到 preoptions 和 postoptions 之间
    if [ -n "$input" ]; then
        cmd=( "$prog_bin" $preoptions "$input" $postoptions )
    else
        cmd=( "$prog_bin" $preoptions $postoptions )
    fi
    if [ "$use_perf" = "true" ]; then
        cmd=( "$PERF_SCRIPT" "${cmd[@]}" )
    fi
    
    # echo "执行命令: ${cmd[@]}"
    exec ${cmd[@]}
else
    # stdin 模式
    cmd=( "$prog_bin" $preoptions $postoptions )
    if [ "$use_perf" = "true" ]; then
        cmd=( "$PERF_SCRIPT" "${cmd[@]}" )
    fi
    if [ -n "$input" ]; then
        "${cmd[@]}" < "$input"
    else
        "${cmd[@]}"
    fi
fi
