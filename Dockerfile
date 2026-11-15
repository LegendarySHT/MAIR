# 基础镜像做成可覆盖的：走了国内镜像站的话传
#   --build-arg UBUNTU_IMAGE=docker.1panel.live/library/ubuntu:22.04
# 默认用 Docker Hub 官方名，免得镜像站下线/403 时整份 Dockerfile 都不可用
# （本机实测 docker.1panel.live 现在对 library/ubuntu 返回 403）。
ARG UBUNTU_IMAGE=ubuntu:22.04
FROM ${UBUNTU_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

# ---------------------------------------------------------------------------
# LLVM 版本。默认 22 —— 这是本仓库当前唯一既装了包又能编过 MAIR 的版本：
#
#   15      -> 走 src/instrumentation/llvm15/ 后端，能编过，但那条路上没有 MAIR
#   16 ~ 21 -> ConfigLLVMCompiler.cmake 直接 FATAL（只接受 15 或 22+）
#   22      -> 走 src/instrumentation/llvm23/，MAIR 在这条路上
#   23      -> 目录名虽然叫 llvm23/，实际对齐的是 LLVM 22 的 API
#              (IRBuilder::CollectMetadataToCopy / Driver::isUsingLTO /
#               SanitizerArgs 的构造在 23 上都不存在)，配置能过、编译必失败
#
# 所以 --build-arg LLVM_VERSION=23 不再是"换个后端试试"，而是已知编不过；
# 需要 LLVM 23 时必须先做 API 移植，不是本镜像的事。
# ---------------------------------------------------------------------------
ARG LLVM_VERSION=22
ARG BUILD_TYPE=Debug
ARG EXTRA_CMAKE_ARGS=""

# Install system dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      wget \
      git \
      ninja-build \
      software-properties-common \
      ca-certificates \
      lsb-release \
      curl \
      python3 \
      python3-pip \
      build-essential \
      cmake \
      pkg-config \
      sudo \
      gnupg \
      && rm -rf /var/lib/apt/lists/*

# Add the apt.llvm.org channel and import the GPG key.
# 只挂版本化频道：clang-N / llvm-N-dev 这类包只出现在 llvm-toolchain-jammy-N 里。
# （原来的写法挂的是 -15 和未版本化的 llvm-toolchain-jammy，所以
#  LLVM_VERSION 除了拼路径以外没起任何作用，镜像里永远只有 15。）
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm-snapshot.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-${LLVM_VERSION} main" > /etc/apt/sources.list.d/llvm.list

# Install LLVM, Clang, and dependencies for the requested version
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      clang-${LLVM_VERSION} \
      clang++-${LLVM_VERSION} \
      llvm-${LLVM_VERSION}-dev \
      libclang-${LLVM_VERSION}-dev \
      lld-${LLVM_VERSION} \
      cmake \
      ninja-build \
      && rm -rf /var/lib/apt/lists/*

# Workaround for LLVM CMake directory layout (same as .github/workflows/build/action.yml,
# see https://github.com/llvm/llvm-project/issues/133861)
RUN ln -sf /usr/lib/llvm-${LLVM_VERSION}/lib /usr/lib/lib && \
    ln -sf /usr/lib/llvm-${LLVM_VERSION}/include /usr/lib/include

# Point the wrapper compilers at the version that was actually installed
ENV X_CC=clang-${LLVM_VERSION}
ENV X_CXX=clang++-${LLVM_VERSION}
ENV XSAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-${LLVM_VERSION}

# Copy XSan source code into the image (.dockerignore keeps build*/ out)
WORKDIR /workspace
COPY . /XSan

# Configure and build XSan.
# CC/CXX 必须显式给：ConfigLLVMCompiler.cmake 发现编译器不是同版本 clang 时
# 会打印"找到 clang-N 了，请自己改 CMAKE_C_COMPILER"然后 FATAL_ERROR。
RUN CC=clang-${LLVM_VERSION} CXX=clang++-${LLVM_VERSION} \
    cmake -S /XSan -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -DLLVM_DIR=/usr/lib/llvm-${LLVM_VERSION}/lib/cmake/llvm \
      -DClang_DIR=/usr/lib/llvm-${LLVM_VERSION}/lib/cmake/clang \
      -DCMAKE_CXX_COMPILER=clang++-${LLVM_VERSION} \
      -DCMAKE_C_COMPILER=clang-${LLVM_VERSION} \
      ${EXTRA_CMAKE_ARGS} \
    && cmake --build build -j"$(nproc)"

ENV PATH="/workspace/build:${PATH}"

# # Run all LIT tests (ASan, TSan, MSan, UBSan, XSan)
# CMD cmake --build build --target check-asan && \
#     cmake --build build --target check-tsan && \
#     cmake --build build --target check-msan && \
#     cmake --build build --target check-ubsan-only && \
#     cmake --build build --target check-xsan
