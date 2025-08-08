#!/bin/bash

# Test one file

# Usage: test-one-lit.sh <file>
# Usage: test-one-lit.sh <file> <san>

# Example: test-one-lit.sh test/asan/TestCases/Linux/cuda_test.cpp

XSAN_ROOT=$(cd "$(dirname "$0")/.." && pwd)
XSAN_BUILD_DIR=$XSAN_ROOT/build
XSAN_TEST_DIR=$XSAN_BUILD_DIR/test
XSAN_TEST_SRC_DIR=$XSAN_ROOT/test

declare -A map
map=(
    [asan]="X86_64LinuxConfig"
    [tsan]="X86_64Config"
    [msan]="X86_64"
    [ubsan]="Standalone-x86_64"
    [xsan]="X86_64LinuxConfig"
)

if [ $# -eq 1 ]; then
    file="$1"
    san=""
    for s in asan msan tsan ubsan xsan; do
        if [[ "$file" == *"$s"* ]]; then
            san="$s"
            break
        fi
    done

    # Get the file name from $file
    filename=$(basename "$file")
    # Find the first matching file in XSAN_TEST_DIR
    found_path=$(find "$XSAN_TEST_DIR" -type f -name "*$filename*" | head -n 1)
    if [ -z "$found_path" ]; then
        found_path=$(find "$XSAN_TEST_SRC_DIR" -type f -name "*$filename*" | head -n 1)
    fi
    if [ -n "$found_path" ]; then
        # If found, try to match asan/msan/tsan/ubsan in the path
        for s in asan msan tsan ubsan xsan; do
            if [[ "$found_path" == *"$s"* ]]; then
                san="$s"
                break
            fi
        done
    fi

    if [ -z "$san" ]; then
        echo "Error: None of the keywords asan, msan, tsan, ubsan, xsan found in the file name."
        echo "Please make sure the file path contains one of the keywords, or provide the sanitizer type (asan/msan/tsan/ubsan/xsan) as the second argument."
        exit 1
    fi
elif [ $# -eq 2 ]; then
    file="$1"
    san="$2"
    if [[ ! "$san" =~ ^(asan|msan|tsan|ubsan|xsan)$ ]]; then
        echo "Error: The second argument must be one of asan, msan, tsan, ubsan, or xsan."
        exit 1
    fi
else
    echo "Usage: $0 <file> [asan|msan|tsan|ubsan|xsan]"
    exit 1
fi

config_dir="$XSAN_TEST_DIR/$san/${map[$san]}"


# # Get the file name without the path
# python3 /data/projects/xsan-20/build/src/runtime/bin/llvm-lit \
#     -sv /data/projects/xsan-20/build/test/tsan/X86_64Config \
#     --filter $1

python3 $XSAN_BUILD_DIR/src/runtime/bin/llvm-lit \
    -sv $config_dir \
    --filter $1
