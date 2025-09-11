#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
# strip 最常用的参数是指定输出文件，使用 -o
export PRE_OPT=""
export POST_OPT="-o /tmp/strip-out"
export stdin="false"

$PROG_DIR/../../../run.sh $@
