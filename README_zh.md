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
3. （可选）对编译器施加侵入式补丁：
   * **仅在 XSan 的运行时补丁无法生效时使用。**
   * XSan 的侵入式补丁当前仅支持 clang-15 与 gcc-9.4。如需支持其他版本，请参考补丁内容自行适配。
   * 对 clang-15 应用补丁：
     ```bash
     git clone -b llvmorg-15.0.7 --depth 1 https://github.com/llvm/llvm-project.git /path/to/llvm-source
     cd /path/to/llvm-source
     git apply /path/to/llvm.patch
     ```

     然后按 LLVM 官方指引构建并安装 clang。
   * 对 gcc-9.4.0 应用补丁：

     ```bash
     git clone --depth=1 --branch=releases/gcc-9.4.0 https://gcc.gnu.org/git/gcc.git /path/to/gcc-source
     cd /path/to/gcc-9.4.0-source
     git apply /path/to/gcc.patch
     ```

     然后按 GCC 官方文档构建并安装。

4. 设置环境变量（建议写入 `.bashrc` 等配置文件）：

   ```bash
   export LLVM_DIR=/path/to/llvm-build
   export PATH=$LLVM_DIR/bin:$PATH
   ```

### 构建 / 安装 / 打包 XSan

1. 克隆 XSan 项目：

   ```bash
   git clone https://github.com/Camsyn/XSan.git /path/to/xsan
   ```

   > 上述命令默认克隆 `main` 分支。如需开发分支，请执行：
   >
   > ```bash
   > cd /path/to/xsan; git switch dev-xsan
   > ```

2. 构建 XSan：

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

3. 运行测试（**注意：需在 Release 模式下构建后运行**）：

   ```bash
   cd /path/to/xsan/build
   make check-all     # 运行所有测试
   make check-asan    # 仅运行 ASan 测试
   make check-ubsan   # 仅运行 UBSan 测试
   make check-tsan    # 仅运行 TSan 测试
   ```

4. 使用 XSan（无需安装）：

   ```bash
   export XSAN_DIR=/path/to/xsan
   export PATH=$XSAN_DIR/build:$PATH
   ```

5. （可选）安装 XSan 到系统：
   TODO

   ```bash
   cd /path/to/xsan/build
   make install
   ```

6. （可选）打包为独立归档包：
   TODO

   ```bash
   cd /path/to/xsan/build
   make archive
   ```

   或者直接打包 `build` 目录。

   > 理论上，XSan 支持以独立压缩包的形式发布，包含所需全部二进制文件，用户解压后即可开箱即用，无需安装。

## 使用指南

确保你能在环境中访问 XSan 的二进制文件，无论是通过构建目录、系统安装路径，还是从归档包中解压而得。

> `xclang/xclang++` 与 `xgcc/xg++` 是 XSan 提供的编译器入口，这些是 Clang 与 GCC 的包装器，自动注入所需编译参数并动态打补丁。

1. 查看编译器包装器的帮助信息：

```bash
# 对 Clang-15
/path/to/xclang -h
# 对 GCC
/path/to/xgcc -h
```

2. 像使用正常编译器一样使用 `xclang` / `xgcc`

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

3. 在 XSan 中构建启用的 Sanitizer 集

* 理论上，XSan 可以选配其已支持 Sanitizer集合的任意子集。
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

* 通过传入 `-fsanitize=` 选项显式启用对应的 Sanitizer：

```bash
xclang -fsanitize=address,thread,memory
xgcc -fsanitize=address,thread
```

* 或者在使用 `-xsan` 启动默认配置的基础上，排除部分 Sanitizer，例如：

```bash
xclang -xsan -fno-sanitize=thread
```
