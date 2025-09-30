#!/bin/bash
set -e  # 只要有一步失败，脚本立即退出

rm -rf temp && mkdir temp

# 驱动是 C++
source ../activate_compile_flags.sh "$mode" true

pushd temp

SRC_DIR=../repo
mkdir -p .bin

# 使用 autotools 构建静态 libpng
"$SRC_DIR"/configure --disable-shared --enable-static
make -j$(nproc)

# 编译并链接 fuzzer（用 C++ 编译器以避免缺失 libc++ 符号）
${CXX:-clang++} ${CXXFLAGS} -std=c++11 -I. -I"$SRC_DIR" -c "$SRC_DIR"/contrib/oss-fuzz/libpng_read_fuzzer.cc -o ./fuzzer.o

# 选择静态库路径（libtool 产物通常在 .libs）
PNG_STATIC_LIB=""
if [ -f ./.libs/libpng16.a ]; then
  PNG_STATIC_LIB=./.libs/libpng16.a
elif ls ./.libs/libpng*.a >/dev/null 2>&1; then
  PNG_STATIC_LIB=$(ls ./.libs/libpng*.a | head -n1)
elif ls ./libpng*.a >/dev/null 2>&1; then
  PNG_STATIC_LIB=$(ls ./libpng*.a | head -n1)
else
  echo "错误: 未找到 libpng 静态库" >&2
  exit 1
fi


${CXX:-clang++} ${CXXFLAGS} ./fuzzer.o -o .bin/libpng@ossfuzz \
  ${LIB_FUZZING_ENGINE} "$PNG_STATIC_LIB" -lz -lpthread -stdlib=libc++

echo $CXXFLAGS

popd 

echo "完成: libpng($mode) 构建与产物收集。"