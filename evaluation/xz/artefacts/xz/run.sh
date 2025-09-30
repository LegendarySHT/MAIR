#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)

# 冒烟测试：默认只执行基本操作，避免复杂压缩/解压
: "${SMOKE:=1}"
export stdin="false"

if [ "$SMOKE" = "1" ] || [ "$SMOKE" = "true" ]; then
  # 只测试基本的help信息，不处理文件I/O
  export PRE_OPT="--help"
  export POST_OPT=""
else
  # 正常模式：通过文件传递输入
  # 如果提供了输入参数，则根据扩展名决定压缩/解压，并将输出/解压目录置于 /tmp 下
  if [ $# -ge 2 ]; then
    input="$2"
    TMP_DIR=$(mktemp -d /tmp/xz_bench_XXXXXX)
    # 解压：将输出目录设置到 /tmp 临时目录
    if [[ "$input" == *.xz ]]; then
      export PRE_OPT="-d -c"
      export POST_OPT="> $TMP_DIR/output"
    else
      # 压缩：将生成的压缩包写到 /tmp 临时目录
      export PRE_OPT="-c"
      export POST_OPT="> $TMP_DIR/output.xz"
    fi
  else
    export PRE_OPT=""
    export POST_OPT=""
  fi
fi

$PROG_DIR/../../../run.sh $@