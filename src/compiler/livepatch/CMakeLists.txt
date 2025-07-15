set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# For "util/PatchHelper.h"
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Both patches of clang/gcc rely on LLVM.
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})

# If the dynsym locates in the main executable, the symbol with the same name
# will be masked. Therefore, we need to use -Bsymbolic-functions to ensure the
# symbol is not masked.
# -Bsymbolic: make the refernces to internal dyn symbols to be resolved to the
# relative address.
add_link_options(-Wl,-Bsymbolic-functions)

# Due to the LLVM API, we can only livepatch clang-15.
add_subdirectory(clang-15)

# In theory, the hot patch of gcc does not limit the version of gcc
add_subdirectory(gcc)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/support_dso_injection.sh
  ${XSAN_PATCH_DIR}/support_dso_injection.sh
  @ONLY
)

install(FILES ${XSAN_PATCH_DIR}/support_dso_injection.sh
  DESTINATION ${XSAN_INSTALL_PATCHDIR}
  COMPONENT patch

  PERMISSIONS # 755 permission: rwxr-xr-x
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
)
