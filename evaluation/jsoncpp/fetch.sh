#!/bin/bash

set -euo pipefail
export PROJ=iproute2
URL="https://github.com/open-source-parsers/jsoncpp"

if [ ! -d repo/.git ]; then 
    git clone "$URL" repo --depth 1 --branch master
    pushd repo
    
    # 来自 FuzzBench的 benchmark，按照 fuzzbench/benchmarks/*/benchmark.yaml 的内容控制版本
    git fetch --depth 1 origin 8190e061bc2d95da37479a638aa2c9e483e58ec6
    git checkout 8190e061bc2d95da37479a638aa2c9e483e58ec6
    popd
else 
    echo "已存在 repo，跳过克隆"
fi


# FuzzBench 中 jsoncpp 为空白corpus，为了方便评估性能，我们自建数据集为 jsoncpp下所有的json文件
mkdir -p corpus/jsoncpp@fuzz
echo "FuzzBench 中不存在 jsoncpp 的测试语料，我们自建数据集为 jsoncpp下所有的json文件"
# 过滤掉过于小的文件，避免影响评估性能
find repo -type f -name "*.json" -size +512c -exec cp {} corpus/jsoncpp@fuzz/ \;

# 对于C++程序，由于我们不插桩 libc++, 因此MSan可能有误报；因此，我们手动过滤掉引起误报的测试输入
FP_SEEDS=("pass1.json" "fail_test_stack_limit.json")
for seed in ${FP_SEEDS[@]}; do
    rm -f corpus/jsoncpp@fuzz/$seed
done

