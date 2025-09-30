#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT="enc -aes-256-ctr -in"
export POST_OPT="-out /dev/null -pass pass:mysecretpassword -pbkdf2 -iter 10000"
export stdin="false"

$PROG_DIR/../../../run.sh $@
