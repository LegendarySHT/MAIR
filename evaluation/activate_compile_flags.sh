# -----------------------------------------------------------------------------
# 本脚本：activate_compile_flags.sh
#
# 说明：
#   本脚本用于根据传入的编译模式（mode）和是否为 C++（is_cxx）自动设置编译器相关环境变量，
#   以便在不同 Sanitizer、优化等级、调试等场景下统一管理 C/C++ 工程的编译参数。
#
#   支持的 mode 包括：
#     - dbg                       ：调试模式，关闭优化（-O0）; 否则默认优化（-O2）
#     - asan                      ：开启 AddressSanitizer
#     - ubsan                     ：开启 UndefinedBehaviorSanitizer
#     - tsan                      ：开启 ThreadSanitizer
#     - msan                      ：开启 MemorySanitizer
#     - xsan-asan                 ：XSan 开启 ASan
#     - xsan-asan-tsan            ：XSan 开启 ASan + TSan
#     - xsan-asan-msan            ：XSan 开启 ASan + MSan
#     - xsan-asan-ubsan           ：XSan 开启 ASan + UBSan
#     - xsan-asan-tsan-ubsan      ：XSan 开启 ASan + TSan + UBSan
#     - xsan-asan-msan-tsan       ：XSan 开启 ASan + MSan + TSan
#     - xsan-asan-msan-ubsan      ：XSan 开启 ASan + MSan + UBSan
#     - xsan-asan-msan-tsan-ubsan ：XSan 开启 ASan + TSan + MSan + UBSan
#
#   is_cxx 取值：
#     - "true"   ：C++ 项目，CXXFLAGS/LDFLAGS 增加 -stdlib=libc++
#     - "false"  ：C 项目
#
# 使用方法（Usage）：供 compile.sh 调用
#   source ./activate_compile_flags.sh <mode> <is_cxx>
#
#   例如：
#     source ./activate_compile_flags.sh asan true
#     source ./activate_compile_flags.sh dbg false
#
#   执行后，会自动设置 CC/CXX/CFLAGS/CXXFLAGS/LDFLAGS 等环境变量。
# -----------------------------------------------------------------------------

# 由于 bash 没有原生的枚举类型，通常用字符串或数组来模拟枚举。这里我们用字符串，并在函数内部用 case 语句处理。
# 参数: $1 = mode, $2 = is_cxx (true/false)
mode="$1"
is_cxx="$2"

if command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
    export CC=xclang
    export CXX=xclang++
else
    echo "错误: 未找到 clang/clang++，请确保已在 PATH 中。" >&2
    exit 1
fi

# export CC=clang
# export CXX=clang++

# 默认追加 -g
export CFLAGS="${CFLAGS:+$CFLAGS }-g"
export CXXFLAGS="${CXXFLAGS:+$CXXFLAGS }-g"
export LDFLAGS="${LDFLAGS:+$LDFLAGS }"

# 推荐用 BASH_SOURCE 获取脚本真实路径，兼容 source 和直接执行
SCRIPTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)

export FUZZER_LIB="${SCRIPTS_DIR}/libAFL.a"
export LIB_FUZZING_ENGINE="${SCRIPTS_DIR}/libAFL.a"

# If FUZZER_LIB does not exist, create it by running fetch-gen-driver.sh
if [ ! -f "${SCRIPTS_DIR}/libAFL.a" ]; then
    echo "FUZZER_LIB does not exist, creating it by running fetch-gen-driver.sh"
    ${SCRIPTS_DIR}/fetch-gen-driver.sh
fi

# is_cxx = true 时，CXXFLAGS, LDFLAGS 追加 -stdlib=libc++
if [ "$is_cxx" = "true" ]; then
    export CXXFLAGS="${CXXFLAGS} -stdlib=libc++"
    export LDFLAGS="${LDFLAGS} -stdlib=libc++"
fi

# mode 处理
case "$mode" in
    *dbg)
        export CFLAGS="${CFLAGS} -O0"
        export CXXFLAGS="${CXXFLAGS} -O0"
        ;;
    *)
        export CFLAGS="${CFLAGS} -O2"
        export CXXFLAGS="${CXXFLAGS} -O2"
        ;;
esac

# 从 mode 中去除 -dbg 后缀（如有），便于后续 case 判断
mode="${mode%.dbg}"
mode="${mode%-dbg}"

# sanitizer 相关
case "$mode" in
    asan)
        export CFLAGS="${CFLAGS} -fsanitize=address"
        export CXXFLAGS="${CXXFLAGS} -fsanitize=address"
        export LDFLAGS="${LDFLAGS} -fsanitize=address"
        ;;
    ubsan)
        export CFLAGS="${CFLAGS} -fsanitize=undefined"
        export CXXFLAGS="${CXXFLAGS} -fsanitize=undefined"
        export LDFLAGS="${LDFLAGS} -fsanitize=undefined"
        ;;
    tsan)
        export CFLAGS="${CFLAGS} -fsanitize=thread"
        export CXXFLAGS="${CXXFLAGS} -fsanitize=thread"
        export LDFLAGS="${LDFLAGS} -fsanitize=thread"
        ;;
    msan)
        export CFLAGS="${CFLAGS} -fsanitize=memory"
        export CXXFLAGS="${CXXFLAGS} -fsanitize=memory"
        export LDFLAGS="${LDFLAGS} -fsanitize=memory"
        ;;
    xsan-asan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address"
        ;;
    xsan-asan-tsan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address,thread"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address,thread"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address,thread"
        ;;
    xsan-asan-msan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address,memory"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address,memory"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address,memory"
        ;;
    xsan-asan-ubsan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address,undefined"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address,undefined"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address,undefined"
        ;;
    xsan-asan-tsan-ubsan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address,thread,undefined"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address,thread,undefined"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address,thread,undefined"
        ;;
    xsan-asan-msan-tsan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address,memory,thread"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address,memory,thread"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address,memory,thread"
        ;;
    xsan-asan-msan-ubsan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address,memory,undefined"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address,memory,undefined"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address,memory,undefined"
        ;;
    xsan-asan-msan-tsan-ubsan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address,memory,thread,undefined"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address,memory,thread,undefined"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address,memory,thread,undefined"
        ;;
    raw|'')
        # 不做处理
        ;;
    *)
        echo "警告：未识别的编译选项：$1"
        echo "可选参数包括：raw, asan, tsan, ubsan, msan, xsan-asan, xsan-asan-tsan, xsan-asan-msan, xsan-asan-ubsan, xsan-asan-tsan-ubsan, xsan-asan-msan-tsan, xsan-asan-msan-ubsan, xsan-asan-msan-tsan-ubsan"
        ;;
esac

