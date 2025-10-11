FROM docker.1panel.live/library/ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

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

# Add LLVM APT repositories and import the GPG key
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm-snapshot.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy main" > /etc/apt/sources.list.d/llvm.list && \
    echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-15 main" >> /etc/apt/sources.list.d/llvm.list

# Install LLVM, Clang, and dependencies (v15 is the default for tests)
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      clang-15 \
      clang++-15 \
      llvm-15-dev \
      libclang-15-dev \
      lld-15 \
      cmake \
      ninja-build

# Workaround for LLVM CMake directory layout (see build action)
RUN ln -sf /usr/lib/llvm-15/lib /usr/lib/lib && \
    ln -sf /usr/lib/llvm-15/include /usr/lib/include

# Set build arguments (can be overridden)
ARG BUILD_TYPE=Debug
ARG LLVM_VERSION=15
ARG EXTRA_CMAKE_ARGS="-DXSAN_ENABLE_LIT_TESTS=ON"

# Set environment variables for clang
ENV X_CC=clang-15
ENV X_CXX=clang++-15

# Copy XSan source code into the image
WORKDIR /workspace
COPY . /XSan

# Configure and build XSan
RUN cmake -S /XSan -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -DLLVM_DIR=/usr/lib/llvm-${LLVM_VERSION}/lib/cmake/llvm \
      -DClang_DIR=/usr/lib/llvm-${LLVM_VERSION}/lib/cmake/clang \
      -DCMAKE_CXX_COMPILER=clang++-${LLVM_VERSION} \
      -DCMAKE_C_COMPILER=clang-${LLVM_VERSION} \
      ${EXTRA_CMAKE_ARGS} \
    && cmake --build build -j

ENV PATH="/workspace/build:${PATH}"

# # Run all LIT tests (ASan, TSan, MSan, UBSan, XSan)
# CMD cmake --build build --target check-asan && \
#     cmake --build build --target check-tsan && \
#     cmake --build build --target check-msan && \
#     cmake --build build --target check-ubsan-only && \
#     cmake --build build --target check-xsan

