#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
REGEX='[[:alpha:]][[:ascii:]][[:lower:]]'
export PRE_OPT="-n $REGEX"
export POST_OPT=""
export stdin="false"

export MSAN_OPTIONS="poison_in_malloc=0:poison_in_free=0:poison_in_dtor=0"

$PROG_DIR/../../../run.sh $@
