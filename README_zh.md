# XSan 项目说明文档（README）

[![Build](https://github.com/Camsyn/XSan/actions/workflows/build.yml/badge.svg)](https://github.com/Camsyn/XSan/actions/workflows/build.yml)

## 项目简介

XSan 项目旨在以高效且可扩展的方式组合多种 Sanitizer 工具。该项目基于 LLVM/Clang 编译器基础设施实现，并以 Clang/GCC 插件的形式集成。

目前，XSan 支持以下几种 Sanitizer：

* AddressSanitizer (ASan)
* ThreadSanitizer (TSan)
* MemorySanitizer (MSan)
* UndefinedBehaviorSanitizer (UBSan)

TODO：计划在后续 LLVM 版本中支持更多 Sanitizer，如：

* NSan（NumericSanitizer）
* TySan（TypeSanitizer）
* GWP-ASan

暂不考虑的Sanitizer：

- 非LLVM项目的Sanitizer：XSan使用LLVM框架开发，不方便集成这些Sanitizer
- HWASan、RtSan、MemTagSanitizer: 受限于具体硬件平台
- GWPSan: 架构设计与其它Sanitizer迥异，无法兼容。

## 项目结构

* `src`：XSan 项目的源代码目录

  * `include`：头文件目录
  * `compiler`：编译器包装器模块
  * `runtime`：运行时库（大部分来自 LLVM 的 compiler-rt）

    * `lib`：主要的运行时库实现

    > 几乎所有 Sanitizer 的运行时库均迁移自 [LLVM 提交版本: 3469996](https://github.com/llvm/llvm-project/commit/3469996d0d057d99a33ec34ee3c80e5d4fa3afcb)
    > 注意：UBSan 依赖 Clang 前端实现，因此相关函数检查能力无法升级（我们仍基于 clang-15）
  * `instrumentation`：插桩逻辑实现
* `test`：XSan 项目的回归测试用例

  * `asan`：ASan 的测试用例
  * `ubsan`：UBSan 的测试用例
  * `tsan`：TSan 的测试用例
  * `xsan`：XSan 相关的测试用例

### 分支说明

* `main`：用于插件形式 Sanitizer 的原始代码
* `dev-xsan`：XSan 项目的开发分支（需要切换至本分支）

## 从源码构建

### 先决条件
1. 如果需要从源码构建完整XSan，需要先切换XSan至 dev-xsan 分支：`git switch dev-xsan`
2. 要构建 XSan，需准备 LLVM-15 和 Clang-15 的环境，既可以手动构建，也可以包管理器安装。
     - 若无 clang/LLVM-15 的环境，可以通过以下命令快速安装
      ```bash
      ubuntu_name=jammy # 根据你的 Ubuntu 版本选择, 此为 Ubuntu 22.04 的版本名
      wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
      sudo add-apt-repository "deb http://apt.llvm.org/${ubuntu_name}/ llvm-toolchain-${ubuntu_name} main"
      sudo add-apt-repository "deb http://apt.llvm.org/${ubuntu_name}/ llvm-toolchain-${ubuntu_name}-15 main"
      # Workaround for issue https://github.com/llvm/llvm-project/issues/133861
      sudo ln -s /usr/lib/llvm-15/lib /usr/lib/lib
      sudo ln -s /usr/lib/llvm-15/include /usr/lib/include
      sudo apt-get update
      sudo apt-get install ninja-build clang-15 llvm-15-dev libclang-15-dev
      ```
3. （可选）对编译器施加侵入式补丁：
   * **仅在 XSan 的运行时补丁无法生效时使用。**
   * XSan 的侵入式补丁当前仅支持 clang-15 与 gcc-9.4。如需支持其他版本，请参考补丁内容自行适配。
   * 对 clang-15 应用补丁：
     ```bash
     git clone -b llvmorg-15.0.7 --depth 1 https://github.com/llvm/llvm-project.git /path/to/llvm-source
     cd /path/to/llvm-source
     git apply /path/to/llvm-15.0.7.patch
     ```

     然后按 LLVM 官方指引构建并安装 clang。
   * 对 gcc-9.4.0 应用补丁：

     ```bash
     git clone --depth=1 --branch=releases/gcc-9.4.0 https://gcc.gnu.org/git/gcc.git /path/to/gcc-source
     cd /path/to/gcc-9.4.0-source
     git apply /path/to/gcc-9.4.patch
     ```

     然后按 GCC 官方文档构建并安装。

4. 设置环境变量（建议写入 `.bashrc` 等配置文件）：

   ```bash
   export LLVM_DIR=/path/to/llvm_dir
   # 如果 clang-15 安装在不同于 LLVM-15 的其它目录，你需要设置 Clang_DIR 为 clang-15 的目录
   export Clang_DIR=/path/to/clang_dir
   export PATH=$LLVM_DIR/bin:$PATH
   ```

### 构建 / 测试 / 安装 / 打包 XSan

#### 1. 克隆 XSan 项目：

   ```bash
   git clone https://github.com/Camsyn/XSan.git /path/to/xsan
   ```

   > `dev-xsan` 分支为开发的主分支

#### 2. 构建 XSan：

   > 注意：我们需要 `clang-15` 来编译构建 XSan，可以通过以下手段之一实现
   > - 更改环境变量: 
   >   `export CC=/path/to/clang-15; export CXX=/path/to/clang++-15`
   > - 修改cmake参数：
   >   `cmake -DCMAKE_C_COMPILER=/path/to/clang-15 -DCMAKE_CXX_COMPILER=/path/to/clang++-15 ...`
   >   如果你有 >1 个 clang/LLVM 版本，你应该通过设置环境变量 `Clang_DIR` 和 `LLVM_DIR` 来指定 clang/LLVM 的版本。

   > 注意：若使用侵入式补丁，cmake配置时，需要指定参数 `-DXSAN_USE_LIVEPATCH=OFF` 来禁用热补丁

   * **调试模式**（用于开发和调试）：

   ```bash
   cd /path/to/xsan; mkdir build; cd build
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   make -j$(nproc)
   ```

   * **发布模式**（用于测试和实际使用）：

   ```bash
   cd /path/to/xsan; mkdir build; cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make -j$(nproc)
   ```

   > 如有 Ninja，也可以使用 `cmake -G Ninja ... && ninja` 来构建

#### 3. 运行测试：

- 先决依赖：
   本项目的测试依赖于 LLVM LIT + FileCheck。
   请确保你的环境下可以找到 `FileCheck` 等LLVM测试工具。

   ```bash
   FileCheck --version
   ```
   若缺失 `FileCheck`, 按以下步骤进行修复：
   ```bash
   # 若 $LLVM_DIR/bin/FileCheck 存在，直接符号链接
   if [ -f $LLVM_DIR/bin/FileCheck ]; then
       sudo ln -s $LLVM_DIR/bin/FileCheck /usr/bin/FileCheck
       sudo ln -s $LLVM_DIR/bin/not /usr/bin/not
       sudo ln -s $LLVM_DIR/bin/count /usr/bin/count
   # 若 $LLVM_DIR/bin/FileCheck 不存在，尝试从包管理器获取
   else
       sudo apt-get install llvm-15-tools
   fi
   ```

- 运行测试：

   ```bash
   cd /path/to/xsan/build
   make check-all     # 运行所有测试
   make check-asan    # 仅运行 ASan 测试
   make check-msan    # 仅运行 MSan 测试
   make check-ubsan   # 仅运行 UBSan 测试
   make check-tsan    # 仅运行 TSan 测试
   make check-xsan    # 仅运行为 Xsan 单独设计的测试
   ```

- 可能的合理测试失败原因及相关解决方案：
   
   - ASan有5个测试用例在**Debug模式**下可能会 Unexpected Pass：可忽略或通过 Release 构建XSan来解决
      - 特征： Unexpected Pass
   
   - 如果以**非标准环境 (即非 Ubuntu + Docker) **运行测试，由于权限或API不支持等原因，部分测试用例可能也会失败，请甄别。
      - 典型特征：报错提示为权限不足或者assert失败
      - 如不支持 `process_vm_readv` 、`name_to_handle_at` 等底层API时，针对这些API的测试会失败
      - 非 ROOT 权限运行测试：部分需要ROOT权限的测试会失败
   
   - `$LLVM_DIR/bin/llvm-symbolizer` 没有启用 zlib 支持，导致符号化失败，进而导致测试的检查失败。该问题常见于那些静态链接的 `llvm-symbolizer` ，如 LLVM GitHub Release下下载的 `clang+llvm` 压缩包，或 `-DBUILD_SHARED_LIBS=OFF`的本地LLVM构建。可以忽略，或者通过以下方法解决
      - 特征：报错  `error: failed to decompress '.debug_aranges', zlib is not available`  
      - 可以通过如下命令进行验证：`echo 'CODE "/lib/x86_64-linux-gnu/libc.so.6" 0x2a1c9 ' | $LLVM_DIR/bin/llvm-symbolizer`
      - 若`$LLVM_DIR/bin/llvm-symbolizer`确实没有启用 zlib 支持，可以通过提前设置环境变量 `XSAN_SYMBOLIZER_PATH=/path/to/llvm-symbolizer-with-zlib-support` ，强制改变LIT测试所用的 `llvm-symbolizer` 来解决该问题,
         - 若本地无 `llvm-symbolizer-with-zlib-support`，可以通过 `sudo apt-get install llvm-15` 来安装带有 zlib 支持的 `llvm-symbolizer` 或者自己本地构建LLVM
   

#### 4. 使用 XSan（无需安装）：

   ```bash
   export XSAN_DIR=/path/to/xsan
   export PATH=$XSAN_DIR/build:$PATH
   ```

#### 5. （可选）安装 XSan 到系统：

   - 可通过 `cmake -DCMAKE_INSTALL_PREFIX=/path/to/install` 来自定义安装目录

   ```bash
   cd /path/to/xsan/build
   make install
   ```

#### 6. （可选）打包为独立归档包：

   ```bash
   cd /path/to/xsan/build
   make package
   ```

   或者直接打包 `build` 目录。

   > 理论上，XSan 支持以独立压缩包的形式发布，包含所需全部二进制文件，用户解压后即可开箱即用，无需安装。

## 使用指南

> 详细用法可以参见我们的回归测试 或 GitHub CI/CD 配置(`.github`).

确保你能在环境中访问 XSan 的二进制文件，无论是通过构建目录、系统安装路径，还是从归档包中解压而得。

> `xclang/xclang++` 与 `xgcc/xg++` 是 XSan 提供的编译器入口，这些是 Clang 与 GCC 的包装器，自动注入所需编译参数并动态打补丁。

### 0. 检查编译器版本

`xclang` 只支持 clang-15，`xgcc`则支持多个gcc版本

- `xclang` 默认会使用 `clang` 来编译项目（需要配置 `PATH` 环境变量）, 请保证 `clang` 版本为 15
- 也可以在使用 `xclang` 之前通过设置环境变量来手动指定底层编译器
   ```bash
   export X_CC=/path/to/clang-15
   export X_CXX=/path/to/clang++-15
   ```
   
- 若无 clang/LLVM-15 的环境，可以通过以下命令快速安装
   ```bash
   ubuntu_name=jammy # 根据你的 Ubuntu 版本选择, 此为 Ubuntu 22.04 的版本名
   wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
   sudo add-apt-repository "deb http://apt.llvm.org/${ubuntu_name}/ llvm-toolchain-${ubuntu_name} main"
   sudo add-apt-repository "deb http://apt.llvm.org/${ubuntu_name}/ llvm-toolchain-${ubuntu_name}-15 main"
   # Workaround for issue https://github.com/llvm/llvm-project/issues/133861
   sudo ln -s /usr/lib/llvm-15/lib /usr/lib/lib
   sudo ln -s /usr/lib/llvm-15/include /usr/lib/include
   sudo apt-get update
   sudo apt-get install ninja-build clang-15 llvm-15-dev libclang-15-dev
   ```

### 1. 查看编译器包装器的帮助信息：

```bash
# 对 Clang-15
/path/to/xclang -h
# 对 GCC
/path/to/xgcc -h
```

### 2. 像使用正常编译器一样使用 `xclang` / `xgcc`

* 理论上，我们的包装器支持原始编译器所支持的所有编译选项。

* 若希望主动启用 XSan 支持的所有 Sanitizer，可使用以下方式：

    ```bash
    xclang -xsan ...
    xgcc -xsan ...
    ```

* 若你通过 `xclang` / `xgcc` 显式启用若干“互不兼容”的 Sanitizer，XSan 将自动接管并支持其组合，如下所示：

   ```bash
   # 自动启用 XSan 以组合支持 ASan、TSan 和 MSan
   xclang -fsanitize=address,thread,memory
   
   # GCC 不支持 MemorySanitizer
   xgcc -fsanitize=address,thread
   
   # ASan 与 UBSan 兼容，以下命令等价于：
   # clang -fsanitize=address,undefined
   xclang -fsanitize=address,undefined
   ```
   
   
   * 注意：ASan / TSan / MSan 彼此不兼容；UBSan 与所有其他 Sanitizer 均兼容，但会引入一定性能开销。
   


---

### 3. 在 XSan 中启用特定的 Sanitizer 集合

   * 理论上，XSan 可以选配其已支持 Sanitizer集合的任意子集（ASan + TSan + MSan + UBSan）。

   * XSan 支持在 *构建时* 和 *编译时* 配置启用哪些 Sanitizer：

**构建时配置（XSan-build time）：**

> 以下示例展示如何在构建阶段禁用 TSan：

* 可通过修改 `xsan_config.cmake` 中的选项控制启用状态：

    ```cmake
    option(XSAN_CONTAINS_TSAN "Enable ThreadSanitizer (TSan) globally" ON)
    ```

* 或在执行 CMake 配置阶段传入构建选项：

    ```bash
    cmake -DXSAN_CONTAINS_TSAN=OFF -DCMAKE_BUILD_TYPE=Release ..
    ```

**编译时配置（Compile time）：**

XSan 根据编译参数来选择需要进行的前端处理和插桩，如 `-fsanitize=address,thread` 仅进行ASan和TSan相关的前端处理和插桩  ；通过链接参数来选择要链接的运行时（例如 `-fsanitize=address,memory` 只会链接仅实现 ASan + MSan 功能的最简运行时）

* 通过传入 `-fsanitize=` 选项显式启用对应的 Sanitizer：

    ```bash
    xclang -fsanitize=address,thread,memory
    xgcc -fsanitize=address,thread
    ```

* 或者在使用 `-xsan` 启动默认配置的基础上，排除部分 Sanitizer，例如：

    ```bash
    xclang -xsan -fno-sanitize=thread
    ```

### 4. 动态/静态运行时库支持

XSan 同时支持静态链接或动态链接运行时库，用法于正常编译器保持一致

- `xclang -shared-libsan` : 动态链接XSan运行时
- `xclang -static-libsan` : 静态链接XSan运行时
> gcc 本不支持选项 `-static-libsan`/`-shared-libsan`, 为了使用体验上的一致性，我们在 `xgcc` 中支持了这两个选项。

注意：我们的包装器与底层编译器的行为保持一致，即
- `xclang` 默认静态链接XSan运行时。
- `xgcc` 默认动态链接XSan运行时。
- 无法动态链接 支持 MSan 的 XSan运行时，因为 MSan 本身就不支持动态链接。
