# -----------------------------------------------------------------------------
# Script: activate_compile_flags.sh
#
# Description:
#   Configure compiler-related environment variables from the selected build mode (mode) and C++ flag (is_cxx),
#   so C/C++ flags are managed consistently across Sanitizers, optimization levels, and debug configurations.
#
#   Supported modes include:
#     - dbg                       : debug mode, disable optimizations (-O0); otherwise default to -O2
#     - asan                      : enable AddressSanitizer
#     - ubsan                     : enable UndefinedBehaviorSanitizer
#     - tsan                      : enable ThreadSanitizer
#     - msan                      : enable MemorySanitizer
#     - xsan-asan                 : XSan with ASan
#     - xsan-asan-tsan            : XSan with ASan + TSan
#     - xsan-asan-msan            : XSan with ASan + MSan
#     - xsan-asan-ubsan           : XSan with ASan + UBSan
#     - xsan-asan-tsan-ubsan      : XSan with ASan + TSan + UBSan
#     - xsan-asan-msan-tsan       : XSan with ASan + MSan + TSan
#     - xsan-asan-msan-ubsan      : XSan with ASan + MSan + UBSan
#     - xsan-asan-msan-tsan-ubsan : XSan with ASan + TSan + MSan + UBSan
#     - asan-mair-build           : ASan (fork `-asan`) with MAIR engaged but no
#                                   MAIR transform (sets MAIR=build)
#     - asan-mair-opt             : ASan (fork `-asan`) with the full MAIR
#                                   pipeline (sets MAIR=1)
#
#   is_cxx values:
#     - "true"   : C++ project; append -stdlib=libc++ to CXXFLAGS/LDFLAGS,
#                   unless the probe below finds no libc++ on this machine
#     - "false"  : C project
#
# Usage: meant to be sourced by compile.sh
#   source ./activate_compile_flags.sh <mode> <is_cxx>
#
#   Examples:
#     source ./activate_compile_flags.sh asan true
#     source ./activate_compile_flags.sh dbg false
#
#   After sourcing, CC/CXX/CFLAGS/CXXFLAGS/LDFLAGS and related variables are configured.
# -----------------------------------------------------------------------------

# Bash has no native enum type; we emulate it with strings and case branches.
# Args: $1 = mode, $2 = is_cxx (true/false)
if [ $# -eq 1 ] && { [ "$1" = "true" ] || [ "$1" = "false" ]; }; then
    mode="raw"
    is_cxx="$1"
else
    mode="$1"
    is_cxx="$2"
fi

if command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
    export CC=xclang
    export CXX=xclang++
else
    echo "Error: clang/clang++ not found, please ensure they are in PATH." >&2
    exit 1
fi

# export CC=clang
# export CXX=clang++

# For sanitizer headers: this MAIR build tree ships include/{sanitizer,fuzzer,profile}
# while the system LLVM kit's resource directory has no sanitizer/ at all, and
# abseil includes <sanitizer/lsan_interface.h> directly. Add the tree next to
# xclang with -isystem instead of -resource-dir: the directory holds no clang
# builtin headers, and replacing the resource directory would lose stddef.h.
MAIR_INCLUDE_GUARD=$(dirname "$(command -v xclang 2>/dev/null || echo /nonexistent)")
if [ -d "${MAIR_INCLUDE_GUARD}/include/sanitizer" ]; then
    export CFLAGS="${CFLAGS} -isystem ${MAIR_INCLUDE_GUARD}/include"
    export CXXFLAGS="${CXXFLAGS} -isystem ${MAIR_INCLUDE_GUARD}/include"
fi

# Always append -g by default
export CFLAGS="${CFLAGS:+$CFLAGS }-g"
export CXXFLAGS="${CXXFLAGS:+$CXXFLAGS }-g"
export LDFLAGS="${LDFLAGS:+$LDFLAGS }"

# Use BASH_SOURCE to locate the real script path; works for both source and exec
SCRIPTS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)

export FUZZER_LIB="${SCRIPTS_DIR}/libAFL.a"
export LIB_FUZZING_ENGINE="${SCRIPTS_DIR}/libAFL.a"

# If FUZZER_LIB does not exist, create it by running fetch-gen-driver.sh
if [ ! -f "${SCRIPTS_DIR}/libAFL.a" ]; then
    echo "FUZZER_LIB does not exist, creating it by running fetch-gen-driver.sh"
    ${SCRIPTS_DIR}/fetch-gen-driver.sh
fi

# libc++ is not installed on every machine. Probe it and record the result in
# MAIR_LIBCXX_FLAG so package scripts that append -stdlib=libc++ by hand can use
# the same decision instead of failing at link time.
# The probe has to link, not just compile: an empty main compiles without -lc++
# and only the link reports the missing library.
if printf 'int main(){}\n' | xclang++ -x c++ -stdlib=libc++ - -o /tmp/xsan-libcxx-probe 2>/dev/null; then
    MAIR_LIBCXX_FLAG=-stdlib=libc++
else
    MAIR_LIBCXX_FLAG=
    if [ -z "${XSAN_LIBCXX_NOTED:-}" ]; then
        echo "note: libc++ is unavailable for xclang++, using the default C++ library" >&2
        export XSAN_LIBCXX_NOTED=1
    fi
fi
export MAIR_LIBCXX_FLAG

# When is_cxx = true, append the libc++ flag (empty when the probe failed)
if [ "$is_cxx" = "true" ]; then
    export CXXFLAGS="${CXXFLAGS} ${MAIR_LIBCXX_FLAG}"
    export LDFLAGS="${LDFLAGS} ${MAIR_LIBCXX_FLAG}"
fi

# Handle optimization level based on mode (dbg vs non-dbg)
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

# Strip any -dbg suffix from mode so the sanitizer case below is simpler
mode="${mode%.dbg}"
mode="${mode%-dbg}"

# Sanitizer-specific flags
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
    asan-new)
        export CFLAGS="${CFLAGS} -asan"
        export CXXFLAGS="${CXXFLAGS} -asan"
        export LDFLAGS="${LDFLAGS} -asan"
        ;;
    ubsan-new)
        export CFLAGS="${CFLAGS} -ubsan"
        export CXXFLAGS="${CXXFLAGS} -ubsan"
        export LDFLAGS="${LDFLAGS} -ubsan"
        ;;
    tsan-new)
        export CFLAGS="${CFLAGS} -tsan"
        export CXXFLAGS="${CXXFLAGS} -tsan"
        export LDFLAGS="${LDFLAGS} -tsan"
        ;;
    msan-new)
        export CFLAGS="${CFLAGS} -msan"
        export CXXFLAGS="${CXXFLAGS} -msan"
        export LDFLAGS="${LDFLAGS} -msan"
        ;;
    xsan-asan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=address"
        ;;
    xsan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=address,thread"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=address,thread"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=thread"
        ;;     
    xsan-tsan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=thread"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=thread"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=thread"
        ;;
    xsan-msan)
        export CFLAGS="${CFLAGS} -xsan-only -fsanitize=memory"
        export CXXFLAGS="${CXXFLAGS} -xsan-only -fsanitize=memory"
        export LDFLAGS="${LDFLAGS} -xsan-only -fsanitize=memory"
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
    asan-mair-build|asan-mair-opt)
        # The MAIR-enabled ASan pass is the one behind the fork's own `-asan`
        # flag (the same flag evaluation calls `asan-new`); the stock
        # `-fsanitize=address` path ignores MAIR entirely (measured: 0
        # mair.observation GEPs even with MAIR=1).
        # MAIR itself is read from the compiler's process environment, not from
        # CFLAGS: the pass calls getenv("MAIR"), a driver flag would not reach it.
        export CFLAGS="${CFLAGS} -asan"
        export CXXFLAGS="${CXXFLAGS} -asan"
        export LDFLAGS="${LDFLAGS} -asan"
        if [ "$mode" = "asan-mair-build" ]; then
            export MAIR=build
        else
            export MAIR=1
        fi
        ;;
    raw|'')
        # Do not modify flags for raw mode
        ;;
    *)
        echo "Warning: unrecognized compile mode: $1" >&2
        echo "Supported modes include: raw, asan, tsan, ubsan, msan, xsan-asan, xsan-asan-tsan, xsan-asan-msan, xsan-asan-ubsan, xsan-asan-tsan-ubsan, xsan-asan-msan-tsan, xsan-asan-msan-ubsan, xsan-asan-msan-tsan-ubsan, asan-mair-build, asan-mair-opt" >&2
        ;;
esac

if [ ! -z "$PROF" ]; then
    export LDFLAGS="${LDFLAGS} -lprofiler"
fi
