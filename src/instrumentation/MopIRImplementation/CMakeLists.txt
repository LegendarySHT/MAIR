# MopIRImplementation CMakeLists.txt
# 
# 独立的 Pass Manager 实现，不依赖大项目
# 可选支持 LLVM

cmake_minimum_required(VERSION 3.15)

# 设置项目名称
project(MopIRImplementation)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含目录
set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")

# 头文件
set(HEADERS
  ${INCLUDE_DIR}/Pass.h
  ${INCLUDE_DIR}/PassManager.h
  ${INCLUDE_DIR}/PassContext.h
  ${INCLUDE_DIR}/IRUnit.h
  ${INCLUDE_DIR}/ExamplePass.h
)

# 可选：LLVM 支持
option(MOP_IR_USE_LLVM "Enable LLVM support" OFF)

if(MOP_IR_USE_LLVM)
  # 查找 LLVM
  find_package(LLVM REQUIRED CONFIG)
  
  # 添加 LLVM 头文件
  list(APPEND HEADERS
    ${INCLUDE_DIR}/LLVM/LLVM.h
    ${INCLUDE_DIR}/LLVM/LLVMIRUnit.h
    ${INCLUDE_DIR}/LLVM/LLVMPassContext.h
    ${INCLUDE_DIR}/LLVM/LLVMPass.h
    ${INCLUDE_DIR}/LLVM/LLVMPassManager.h
  )
  
  # 添加 LLVM 定义
  add_definitions(-DMOP_IR_USE_LLVM)
  
  # 包含 LLVM 目录
  include_directories(${LLVM_INCLUDE_DIRS})
  
  message(STATUS "LLVM support enabled: ${LLVM_PACKAGE_VERSION}")
endif()

# 源文件（如果有实现文件）
set(SOURCES
  # 目前所有实现都在头文件中，如果有 .cpp 文件，在这里添加
)

# 创建库（如果需要）
# add_library(MopIRImplementation STATIC ${HEADERS} ${SOURCES})
# target_include_directories(MopIRImplementation PUBLIC ${INCLUDE_DIR})
# if(MOP_IR_USE_LLVM)
#   target_link_libraries(MopIRImplementation LLVM)
# endif()

# 示例程序
if(BUILD_EXAMPLES)
  add_executable(simple_example examples/simple_example.cpp)
  target_include_directories(simple_example PRIVATE ${INCLUDE_DIR})
  target_compile_features(simple_example PRIVATE cxx_std_17)
  
  # LLVM 示例（如果启用了 LLVM）
  if(MOP_IR_USE_LLVM)
    add_executable(llvm_example examples/llvm_example.cpp)
    target_include_directories(llvm_example PRIVATE ${INCLUDE_DIR})
    target_compile_features(llvm_example PRIVATE cxx_std_17)
    target_link_libraries(llvm_example LLVM)
  endif()
endif()

# 安装规则（可选）
# install(FILES ${HEADERS} DESTINATION include/MopIRImplementation)
# install(TARGETS MopIRImplementation DESTINATION lib)

