#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT=""
export POST_OPT="-V"
export stdin="false"

MODE="${1:-raw}"
shift 1 || true

# 若提供参数，则按单次运行；否则执行多组常见命令组合
if [ $# -gt 0 ]; then
	PRE_OPT="$*"
	export PRE_OPT
	export MSAN_OPTIONS="poison_in_free=0:poison_in_malloc=0"
	echo "运行: $PROG_DIR/../../../run.sh $MODE (PRE_OPT=\"$PRE_OPT\")"
	exec "$PROG_DIR/../../../run.sh" "$MODE"
else
	CASES=(
		# ""            # 默认
		"-az"         # 全部/压缩
		"-j"          # JSON 输出  
		"-j"          # JSON 输出 (重复1)
		"-j"          # JSON 输出 (重复2)
		"-j"          # JSON 输出 (重复3)
		"-j"          # JSON 输出 (重复4)
		"-j"          # JSON 输出 (重复5)
		"-s"          # 摘要
		# "-r"          # 重置计数（可能需要权限）
		"-t 1 -c 2"   # 周期采样两次
	)
	export MSAN_OPTIONS="poison_in_free=0:poison_in_malloc=0"
	for OPT in "${CASES[@]}"; do
		PRE_OPT="$OPT"
		export PRE_OPT
		echo "========== nstat $PRE_OPT =========="
		"$PROG_DIR/../../../run.sh" "$MODE" || true
		printf "\n"
	done
fi

