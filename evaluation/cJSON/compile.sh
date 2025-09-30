#!/bin/bash
set -e  # 只要有一步失败，脚本立即退出

export PROJ=pcre2


rm -rf temp && mkdir temp

source ../activate_compile_flags.sh $mode false

pushd temp
rm -rf *
cmake ../repo -DBUILD_SHARED_LIBS=OFF
cmake --build . -j$(nproc)

# 将 binutils/ 下的指定可执行文件移动到 ../artefacts 目录
mkdir -p .bin
cp fuzzing/fuzz_main .bin/cJSON@fuzz_main

popd
