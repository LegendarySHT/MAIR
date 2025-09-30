#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT=""
export POST_OPT=""
export stdin="false"

# 让 MSan 非致命并降低噪音（注意: 仍可能报告，但不退出）
export MSAN_OPTIONS="poison_in_dtor=0:poison_in_malloc=0:poison_in_free=0"


$PROG_DIR/../../../run.sh $@
