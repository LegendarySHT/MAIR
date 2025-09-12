#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
# nm-new 常用参数包括 -A, -g, -n, -C 等，用于显示符号表信息
export PRE_OPT=""
export POST_OPT=""
export stdin="false"

$PROG_DIR/../../../run.sh $@
