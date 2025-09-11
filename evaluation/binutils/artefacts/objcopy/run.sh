#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
# objcopy 通常不通过 stdin 传递输入，而是通过命令行参数指定输入输出文件
# 最常用参数为 -O <目标格式>，如 -O binary
export PRE_OPT="-O binary"
export POST_OPT="/tmp/objcopy.out"
export stdin="false"

$PROG_DIR/../../../run.sh $@
