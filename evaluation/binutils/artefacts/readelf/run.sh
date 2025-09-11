#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT="-a"
export POST_OPT=""
export stdin="false"

$PROG_DIR/../../../run.sh $@
