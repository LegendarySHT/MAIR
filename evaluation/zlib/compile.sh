#!/bin/bash
set -e  # Exit immediately if any command fails

rm -rf temp && mkdir temp

source ../activate_compile_flags.sh "$mode" true # is_cxx = true, as the driver is C++

pushd temp

cmake ../repo -DCMAKE_BUILD_TYPE=RelWithDebInfo -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_SHARED=OFF
# Only the static library is needed here. `all` also builds test/infcover, which
# is compiled with -coverage and needs libclang_rt.profile.a, a runtime this
# machine's system LLVM kit does not ship.
make -j zlibstatic

mkdir -p .bin


set -x;

$CXX $CXXFLAGS $LDFLAGS -std=c++11 -I../repo ../zlib_uncompress_fuzzer.cc \
-o .bin/zlib@zlib_uncompress_fuzzer $LIB_FUZZING_ENGINE ./libz.a

# The test target is named minigzipstatic; keep the historical artefact name.
if [ -f test/minigzipstatic ]; then
  cp test/minigzipstatic .bin/minigzip
fi

popd 
