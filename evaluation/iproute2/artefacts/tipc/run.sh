#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)

# TIPC工具配置 - 即使内核模块未加载，仍可测试sanitizer开销
export stdin="false"
export MSAN_OPTIONS="poison_in_free=0:poison_in_malloc=0"

# 使用简单的帮助参数，即使报错也能测试sanitizer性能
export PRE_OPT=""
export POST_OPT="-h"

$PROG_DIR/../../../run.sh $@

