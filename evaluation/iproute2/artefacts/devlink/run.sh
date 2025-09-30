#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT=""
export POST_OPT=""
export stdin="false"

MODE="${1:-raw}"
shift 1 || true

# 将子命令参数放入 PRE_OPT，避免被外层 run.sh 当作“输入文件”
if [ $# -eq 0 ]; then
	PRE_OPT="port show"
else
	PRE_OPT="$*"
fi

export MSAN_OPTIONS="poison_in_free=0:poison_in_malloc=0"


echo "运行: $PROG_DIR/../../../run.sh $MODE (PRE_OPT=\"$PRE_OPT\")"
exec "$PROG_DIR/../../../run.sh" "$MODE"

