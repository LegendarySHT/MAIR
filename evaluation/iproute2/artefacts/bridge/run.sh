#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT=""
export POST_OPT="link show"
export stdin="false"

$PROG_DIR/../../../run.sh $@

