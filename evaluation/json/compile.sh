#!/bin/bash
set -e  # 只要有一步失败，脚本立即退出

export PROJ=pcre2


rm -rf temp && mkdir temp

source ../activate_compile_flags.sh $mode true


# 这是一个 C++的 头文件库项目，因此编译方式比较特殊
# 该目录包含 json 的 fuzz 接口
mkdir -p temp/.bin
pushd repo/tests

# 1. 我们仅仅对项目本体进行插桩，而不对 libc++.so 进行插桩。
# 2. parse_afl_fuzzer 和 parse_bson_fuzzer 会调用 std::to_string 这一个定义在 string.cpp@libc++ 里的符号
# 3. 因此，MSan 会由于认为 std::to_string 返回的 std::string 未初始化而产生误报
# 4. 因此，而MSan报告时会严重增加 overhead，出于公平考虑，我们将 parse_afl_fuzzer parse_bson_fuzzer 排除
BINARIES=(
    parse_afl_fuzzer
    parse_bson_fuzzer
    parse_cbor_fuzzer 
    parse_msgpack_fuzzer 
    parse_ubjson_fuzzer 
    parse_bjdata_fuzzer
)

# 我们仅仅对项目本体进行插桩，而不对 libc++.so 进行插桩。
# 一般而言，使用 MSAN_OPTIONS="poison_in_malloc=0:poison_in_free=0:poison_in_dtor=0" 
# 可以绕过大部分由于 libc++ 未插桩引起的误报。
make -j ${BINARIES[@]}


for bin in "${BINARIES[@]}"; do
    if [ -f "$bin" ]; then
        mv "$bin" "../../temp/.bin/json@$bin"
    fi
done

popd

