#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)

# 冒烟测试：默认只执行基本操作，避免复杂压缩/解压
: "${SMOKE:=1}"
export stdin="false"

if [ "$SMOKE" = "1" ] || [ "$SMOKE" = "true" ]; then
  # 只测试基本的帮助信息，不处理文件I/O
  export PRE_OPT=""
  export POST_OPT="--help"
else
  # 正常模式：如果提供了输入参数，则根据扩展名决定压缩/解压，并将输出/解压目录置于 /tmp 下
  if [ $# -ge 2 ]; then
    input="$2"
    TMP_DIR=$(mktemp -d /tmp/zstd_bench_XXXXXX)
    # 解压：将输出目录设置到 /tmp 临时目录
    if [[ "$input" == *.zst || "$input" == *.zstd ]]; then
      export PRE_OPT="-d -o $TMP_DIR/output"
      export POST_OPT=""
    else
      # 压缩：将生成的压缩包写到 /tmp 临时目录
      export PRE_OPT="-o $TMP_DIR/output.zst"
      export POST_OPT=""
    fi
  else
    export PRE_OPT=""
    export POST_OPT=""
  fi
fi

# # 组合模式 asan+msan：不静音，仅设为非致命；并关闭 LSan 以免与 MSan 冲突
# mode="$1"
# if [[ "$mode" == *asan* && "$mode" == *msan* ]]; then
#   : "${ASAN_OPTIONS:=halt_on_error=0,detect_leaks=0}"
#   : "${MSAN_OPTIONS:=halt_on_error=0}"
#   : "${XSAN_OPTIONS:=halt_on_error=0}"
#   export ASAN_OPTIONS MSAN_OPTIONS XSAN_OPTIONS
# fi

$PROG_DIR/../../../run.sh $@