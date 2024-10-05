# XSan Project README

## Introduction

XSan is a project aiming to compose a set of sanitizers in a efficient and scalable way. The project is based on the LLVM/Clang compiler infrastructure and is implemented as a Clang plugin.

Now, XSan supports the following sanitizers:
- AddressSanitizer (ASan)
- UndefinedBehaviorSanitizer (UBSan)

## Project Structure

- src: the source code of the XSan project.
    - include: the header files of the XSan project.
    - compiler: the compiler wrapper of the XSan project.
    - runtime: the runtime library of the XSan project (major of which come from compiler-rt).
        - lib: the majority of the runtime library.
    - instrumentation: the instrumentation code of the XSan project.
- test: the test cases for the XSan project.

### Branches
- main: the original code for sanitizers used as plugins.
- xsan-dev: the development branch for the XSan project.

## Build

### Prerequisites
1. To build XSan, you need to have the LLVM/Clang-15 source code. You can download the source code from the [LLVM official website](https://github.com/llvm/llvm-project/tree/llvmorg-15.0.7) as follows.
    ```shell
    git clone -b llvmorg-15.0.7 --depth 1 https://github.com/llvm/llvm-project.git /path/to/llvm-source
    ```
    - Because some sanitizer needs the support of compiler frontend, which is not accessible only via the plugin interface, you need to build the whole LLVM/Clang project with some modifications. The modifications are listed in the `llvm.patch` file.
2. Build the LLVM/Clang project with the modifications in the `llvm.patch` file.
    - Apply the patch file to the LLVM/Clang source code.
    ```shell
    cd /path/to/llvm-source
    git apply /path/to/llvm.patch
    ```
    - Build the LLVM/Clang project with the following commands:
      > Note that LLVM15 requires a newer version of gcc/g++ (>= 12.0) to build.
    ```shell
    cd /path/to/llvm-build
    cmake \
        -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;compiler-rt;libcxx;libcxxabi;lld' \
        -DLLVM_ENABLE_PLUGINS=ON \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLLVM_BINUTILS_INCDIR=/usr/include/ \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        /path/to/llvm-source/llvm/
    make -j$(nproc)
    ```
3. Export the relevant environment variables to your dev env (e.g., in .bashrc).
    ```shell
    export LLVM_DIR=/path/to/llvm-build
    export PATH=$LLVM_DIR/bin:$PATH
    ```
### Build XSan
1. Clone the XSan project.
    ```shell
    git clone https://github.com/Camsyn/XSan.git /path/to/xsan
    ```
    > Note that the above command will only clone the main branch of the XSan project. If you want to clone the development branch, you need to switch to the `dev-xsan` branch by executing the following command.
    > ```shell
    > cd /path/to/xsan; git checkout dev-xsan
    > ```
2. Build XSan.
    - Debug mode: use to develop and debug the project.
    ```shell
    cd /path/to/xsan; mkdir build; cd build
    cmake -DCMAKE_BUILD_TYPE=Debug ..
    make -j$(nproc)
    ```
    - Release mode: use to test and apply of the project.
    ```shell
    cd /path/to/xsan; mkdir build; cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make -j$(nproc)
    ```
3. Export the relevant environment variables to your dev env (e.g., in .bashrc).
    ```shell
    export XSAN_DIR=/path/to/xsan
    export PATH=$XSAN_DIR/build:$PATH
    ```
4. Test the XSan (NOTE THAT you need to compile XSan in RELEASE mode).
    ```shell
    cd /path/to/xsan/build
    make check-all # check all the test cases
    make check-asan # check the ASan test cases
    make check-ubsan # check the UBSan test cases
    make check-tsan # check the TSan test cases
    ```