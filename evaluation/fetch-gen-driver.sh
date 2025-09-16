#!/bin/bash

# 推荐用 BASH_SOURCE 获取脚本真实路径，兼容 source 和直接执行
SCRIPTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)

pushd ${SCRIPTS_DIR}

# Download and compile AFL v2.57b.
# Set AFL_NO_X86 to skip flaky tests.
git clone \
        --depth 1 \
        --branch v2.57b \
        https://github.com/google/AFL.git afl

pushd afl
# Use afl_driver.cpp from LLVM as our fuzzing library.
wget https://raw.githubusercontent.com/llvm/llvm-project/5feb80e748924606531ba28c97fe65145c65372e/compiler-rt/lib/fuzzer/afl/afl_driver.cpp -O afl_driver.cpp
clang -Wno-pointer-sign -c llvm_mode/afl-llvm-rt.o.c -Iafl
clang++ -stdlib=libc++ -std=c++11 -O2 -c afl_driver.cpp
ar r ../libAFL.a *.o
popd

rm -rf afl

popd
