cmake_minimum_required(VERSION 3.13)
project(SimpleMopConverter)

# 寻找LLVM包
find_package(LLVM REQUIRED CONFIG)

# 添加LLVM头文件目录
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# 添加MopIR头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
# 添加Utils目录（ValueUtils.h所在位置）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../Utils)

# 获取MopIR库的源文件
file(GLOB MOP_IR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lib/*.cpp")

# 构建MopIR库
add_library(XSanMopIR STATIC ${MOP_IR_SOURCES})

# 创建可执行文件
add_executable(SimpleMopConverter SimpleMopConverter.cpp)

# 获取LLVM组件 - 添加更多必要的组件
llvm_map_components_to_libnames(llvm_libs 
  Support 
  Core 
  IRReader 
  Analysis 
  ScalarOpts 
  TransformUtils 
  InstCombine 
  IPO 
  BitReader 
  Passes
)

# 链接库
target_link_libraries(SimpleMopConverter ${llvm_libs} XSanMopIR)