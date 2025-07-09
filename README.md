# XSan Project README

## Introduction

XSan is a project aiming to compose a set of sanitizers in a efficient and scalable way. The project is based on the LLVM/Clang compiler infrastructure and is implemented as a Clang/GCC plugin.

Now, XSan supports the following sanitizers:
- AddressSanitizer (ASan)
- ThreadSanitizer (TSan)
- MemorySanitizer (MSan)
- UndefinedBehaviorSanitizer (UBSan)
- TODO: support more sanitizers in recent LLVM versions
    - NSan (Numeric Sanitizer)
    - TySan (Type Sanitizer)

TODO: support more sanitizers in recent LLVM versions, such as:
- NSan (NumericSanitizer)
- TySan (TypeSanitizer)
- GWP-ASan

Sanitizers that are not considered:
- Sanitizers from non-LLVM projects: XSan is developed based on the LLVM framework, which makes it difficult to integrate these sanitizers.
- HWASan, RtSan, MemTagSanitizer: Limited by specific hardware platforms.
- GWPSan: The architecture design is significantly different from other sanitizers, making it incompatible.

## Project Structure

- src: the source code of the XSan project.
    - include: the header files of the XSan project.
    - compiler: the compiler wrapper of the XSan project.
    - runtime: the runtime library of the XSan project (major of which come from compiler-rt).
        - lib: the majority of the runtime library.
      > Almost all the sanitizer runtime libraries are migrated from [LLVM21-commit: 3469996] (https://github.com/llvm/llvm-project/commit/3469996d0d057d99a33ec34ee3c80e5d4fa3afcb)
      > Note that UBSan relies on clang frontend, therefore, the function checks of it could not upgrade as we still use clang-15.
    - instrumentation: the instrumentation code of the XSan project.
- test: the regression test cases for the XSan project.
    - asan: the ASan test cases.
    - ubsan: the UBSan test cases.
    - tsan: the TSan test cases.
    - xsan: the XSan-specific test cases.

### Branches
- main: the original code for sanitizers used as plugins.
- dev-xsan: the development branch for the XSan project.

## Build From Source

### Prerequisites
1. If you need to build the complete XSan from the source, first switch XSan to the dev-xsan branch: `git switch dev-xsan`
2. To build XSan, you need to prepare an LLVM-15 and Clang-15 environment. You can either build them manually or install them using a package manager.
3. (Optional) Apply intrusive patches to the compilers.
    - **ONLY for the scenarios that XSan's livepatches do not work with your `clang`/`gcc`.**
    - XSan only provides patches for clang-15 and gcc-9.4. If you require support for other compiler versions, please refer to our patch files and apply the modifications manually.
    - Patch the `clang-15` project with the modifications in the `llvm.patch` file.
        - Apply the patch file to the LLVM/Clang source code.
        ```shell
        git clone -b llvmorg-15.0.7 --depth 1 https://github.com/llvm/llvm-project.git /path/to/llvm-source
        cd /path/to/llvm-source
        git apply /path/to/llvm.patch
        ```
        - Build and install `clang-15` adhering to the guidelines of the compiler project.
    - Patch the `gcc-9.4.0` project with the modifications in the `gcc.patch` file.
        - Apply the patch file to the GCC source code.
        ```shell
        git clone --depth=1 --branch=releases/gcc-9.4.0 https://gcc.gnu.org/git/gcc.git /path/to/gcc-source
        cd /path/to/gcc-9.4.0-source
        git apply /path/to/gcc.patch
        ```
        * Build and install `gcc-9.4.0` adhering to the guidelines of the compiler project.
4. Export the relevant environment variables to your dev env (e.g., in .bashrc).
    ```shell
    export LLVM_DIR=/path/to/llvm-build
    export PATH=$LLVM_DIR/bin:$PATH
    ```
### Build/Install/Archive XSan
1. Clone the XSan project.
    ```shell
    # From GitHub
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
3. Test the XSan (NOTE THAT you need to compile XSan in RELEASE mode).
    ```shell
    cd /path/to/xsan/build
    make check-all # check all the test cases
    make check-asan # check the ASan test cases
    make check-ubsan # check the UBSan test cases
    make check-tsan # check the TSan test cases
    ```
4. Use XSan without installation.
    - Export the relevant environment variables to your dev env (e.g., in .bashrc).
    ```shell
    export XSAN_DIR=/path/to/xsan
    export PATH=$XSAN_DIR/build:$PATH
    ```
5. (Optional) Install XSan to the system.
    TODO
    ```shell
    cd /path/to/xsan/build
    make install
    ```
6. (Optional) Archive XSan to a standalone package.
    TODO
    ```shell
    cd /path/to/xsan/build
    make archive
    ```
    Or you can directly archive the build directory.
    - In theory, XSan supports distributing a standalone package—a compressed archive containing all the necessary binaries—allowing users to run it out of the box after extraction, without requiring installation.

## How to Use

Ensure that the XSan binaries are accessible in your environment, whether they are located in the build directory, installed system-wide, or extracted from the XSan archive.

> `xclang/xclang++` and `xgcc/xg++` are the entry points to the XSan project, which are wrappers upon the real compilers, i.e., `clang` and `gcc`. These wrappers automatically add the compilation parameters required by XSan and actively livepatch the compilers.

1. Access the compiler wrappers (i.e., `xclang` and `xgcc`) for help.
```shell
# For clang-15
/path/to/xclang -h
# For gcc
/path/to/xgcc -h
```

2. Use `xclang`/`xgcc` as a normal compiler.
    - In theory, our wrapper supports all the options originally supported by the compiler.
    - If you wish to actively activate all sanitizers supported by XSan:
    ```shell
    xclang -xsan ...
    xgcc -xsan ...
    ```
    - If you activate some 'incompatible' sanitizers via `xclang`/`xgcc`, XSan will be automatically enabled to support such composition, as follows.
    ```shell
    # XSan is automatically enabled to support the composition of ASan, TSan and MSan
    xclang -fsanitize=address,thread,memory
    # gcc does not support memory
    xgcc -fsanitize=address,thread

    # ASan is compatible with UBSan, hence, this command is equivalent to 
    # `clang -fsanitize=address,undefined`
    xclang -fsanitize=address,undefined
    ```
    - Note: ASan/TSan/MSan are incompatible with each other; UBSan is compatible with other all, but raise some performance overhead.

3. Construct enabled sanitizer set in XSan
- In theory, XSan can choose any subset of the set of sanitizers it supports. 
- XSan supports the selection of sanitizers at XSan-build time and compile time

- XSan-build time:
    > The following shows how to remove TSan during the XSan build phase
    - You can choose whether to enable a sanitizer at xsan_config.cmake by modifying:
    ```cmake
    option(XSAN_CONTAINS_TSAN "Enable ThreadSanitizer (TSan) globally" ON)
    ```
    - Or just transfer cmake options while cmake configure, as follows:
    ```shell
    cmake -DXSAN_CONTAINS_TSAN=OFF -DCMAKE_BUILD_TYPE=xxx ..
    ```
- Compile time:
    - You can choose whether to enable a sanitizer at compile time by adding the following flags:
    ```shell
    xclang -fsanitize=address,thread,memory
    xgcc -fsanitize=address,thread
    ```
    - Or just disable some sanitizers from `-xsan`, as follows:
    ```shell
    xclang -xsan -fno-sanitize=thread
    ```
