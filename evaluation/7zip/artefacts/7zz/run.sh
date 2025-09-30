#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)

# 默认：通过文件传递输入
export stdin="true"

# 如果提供了输入参数，则根据扩展名决定压缩/解压，并将输出/解压目录置于 /tmp 下
if [ $# -ge 2 ]; then
  input="$2"
  TMP_DIR=$(mktemp -d /tmp/7zz_bench_XXXXXX)
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

$PROG_DIR/../../../run.sh $@
