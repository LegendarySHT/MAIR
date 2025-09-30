#!/bin/bash
URL="https://github.com/madler/zlib.git"

# Use depth 1 to avoid cloning the history
git clone $URL repo --depth 1


# Obtain the interface to test the zlib From FuzzBench
if [ ! -f zlib_uncompress_fuzzer.cc ]; then
    wget https://raw.githubusercontent.com/google/fuzzbench/master/benchmarks/zlib_zlib_uncompress_fuzzer/zlib_uncompress_fuzzer.cc
fi


mkdir -p corpus/minigzip

# 通过压缩大于40KB的文件来测试minigzip
find repo/ -type f -size +40k -exec cp {} corpus/minigzip/ \;

# zlib, i.e., /zlib_uncompress_fuzzer, shares the same corpus with minigzip
cp -r corpus/minigzip corpus/zlib
