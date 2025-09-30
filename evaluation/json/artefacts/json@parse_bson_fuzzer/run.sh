#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT=""
export POST_OPT=""
export stdin="true"

# 此为CPP项目，若不插桩 libc++, MSan会产生误报，
# 为了避免该误报，临时使用 poison_in_malloc=0:poison_in_free=0:poison_in_dtor=0
# 禁用堆分配、释放和析构时 MSan 对 shadow 的污染。
export MSAN_OPTIONS="poison_in_malloc=0:poison_in_free=0:poison_in_dtor=0"

$PROG_DIR/../../../run.sh $@
