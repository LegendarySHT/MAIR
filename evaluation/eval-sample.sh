#!/bin/bash

# -----------------------------------------------------------------------------
# 脚本说明：
#   本脚本 eval-sample.sh 用于对指定程序的单次运行进行性能与内存占用测量。
#   通过 perf 和 /usr/bin/time 工具，输出 CPU 时间（task-clock, msec）和最大常驻内存（RSS, KB）。
#
# 用法（Usage）:
#   ./eval-sample.sh <program> <args>...
#
#   <program> ：待测可执行文件路径（如 ./run.sh）
#   <args>    ：传递给 <program> 的参数（如模式、输入文件等）
#
# 示例：
#   ./eval-sample.sh <package>/artefacts/<program>/run.sh asan ./inputs/foo/input1
#
# 输出：
#   CPU time: <msec>
#   RSS     : <KB>
# -----------------------------------------------------------------------------


# 检查参数数量
if [ "$#" -lt 1 ]; then
    echo "用法: $0 <program> [<args>, ...]"
    exit 1
fi


PROGRAM=$1
shift 1

# 检查 perf 是否安装
if ! command -v perf &> /dev/null; then
    echo "错误: perf 未安装。请安装 perf 工具。"
    exit 1
fi

# 检查 /usr/bin/time 是否存在
if [ ! -x /usr/bin/time ]; then
    echo "错误: /usr/bin/time 不可用。"
    exit 1
fi

export ASAN_OPTIONS=detect_leaks=0
export TSAN_OPTIONS=report_bugs=0

# 直接将 perf 和 time 的输出保存到变量，屏蔽程序标准输出
OUTPUT=$(perf stat -- /usr/bin/time -v "$PROGRAM" $@ 2>&1)


# 提取 CPU 时间 (来自 perf 的 task-clock，单位 msec)
CPU_TIME=$(echo "$OUTPUT" | grep "task-clock" | awk '{print $1}')

# 提取最大RSS (来自 /usr/bin/time 的输出，单位 KB)
MAX_RSS=$(echo "$OUTPUT" | grep "Maximum resident set size" | awk '{print $6}')

# 输出最终结果
echo "CPU time: ${CPU_TIME} msec"
echo "RSS     : ${MAX_RSS} KB"
