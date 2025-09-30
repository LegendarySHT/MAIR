#!/bin/bash
set -e
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)
export PRE_OPT=""
export POST_OPT=""
export stdin="false"

# 使用临时目录保存输出与最小配置
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

# 若未提供 strongswan 配置，则写入最小占位配置以避免启动报错
if [ -z "${STRONGSWAN_CONF:-}" ]; then
  cat > "$TMP_DIR/strongswan.conf" <<'EOF'
charon {
  # minimal placeholder config to allow startup
}
EOF
  export STRONGSWAN_CONF="$TMP_DIR/strongswan.conf"
fi

# 调用上层运行器并将输出重定向到临时日志
"$PROG_DIR"/../../../run.sh "$@" > "$TMP_DIR/output.log" 2>&1

# 将日志打印到终端
cat "$TMP_DIR/output.log"
