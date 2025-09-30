#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT=""
export POST_OPT=""
export stdin="true"

# export MSAN_OPTIONS="poison_in_free=0:poison_in_malloc=0"

$PROG_DIR/../../../run.sh $@