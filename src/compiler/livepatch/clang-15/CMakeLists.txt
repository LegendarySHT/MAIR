# Clang is well-designed, exposing lots of functionalities into libraries, e.g.,
# libclang-cpp.so.
#
# We should intercept the following dynamic functions:
# - `SanitizerArgs::SanitizerArgs`: to suppress the error when enable 'incompatible' sanitizer.
# - Could not just intercept the `Driver::Diag`, as we have to guarantee the functionality of
# the enabled sanitizers.
# - `EmitBackendOutput` : to disable the loading of passes.
# - `addSanitizerRuntimes` : to replace the link options of sanitizer runtimes.
# - `PassBuilder::build*Pipeline`:  (Optional) to reconstruct the pass pipeline.

find_package(Clang REQUIRED CONFIG)

include_directories(${CLANG_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# To shared libaray
add_library(clang-patch MODULE
    BackendUtil.cpp
    CommonArgs.cpp
    PassBuilderPipelines.cpp
    SanitizerArgs.cpp
    ../utils/PatchHelper.cpp
)

target_link_libraries(clang-patch PRIVATE dl)

if(LLVM_LINK_LLVM_DYLIB)
    if(NOT CLANG_LINK_CLANG_DYLIB)
        message(FATAL_ERROR "Cannot set CLANG_LINK_CLANG_DYLIB=OFF when "
            "LLVM_LINK_LLVM_DYLIB=ON")
    endif()

    target_compile_definitions(clang-patch PRIVATE -DCLANG_PATCH_LINK_DYLIB)

    if(NOT DEFINED LLVM_SHLIB_SYMBOL_VERSION)
        # "Symbol version prefix for libLLVM.so and libclang-cpp.so"
        set(LLVM_SHLIB_SYMBOL_VERSION "LLVM_${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}")
    endif()

    message(STATUS "LLVM_SHLIB_SYMBOL_VERSION: ${LLVM_SHLIB_SYMBOL_VERSION}")
    configure_file(simple_version_script.map.in simple_version_script.map)

    # ## Seems we don't need to explicitly link clang-cpp and LLVM libraries
    # ## because clang-patch will be injected into the clang process as a dynamic library
    # ## The clang process has already loaded these libraries, so we can use them directly
    # target_link_libraries(clang-patch PRIVATE clang-cpp LLVM)
    target_link_options(clang-patch PRIVATE -Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/simple_version_script.map)
else()
    if(CLANG_LINK_CLANG_DYLIB)
        message(FATAL_ERROR "Cannot set CLANG_LINK_CLANG_DYLIB=ON when "
            "LLVM_LINK_LLVM_DYLIB=OFF")
    endif()

    # ## Seems we don't need to explicitly link clang-cpp and LLVM libraries
    # ## because clang-patch will be injected into the clang process as a dynamic library
    # ## The clang process has already loaded these libraries, so we can use them directly
    # target_link_libraries(clang-patch PRIVATE clangBasic LLVMSupport LLVMOption)
endif()

set_target_properties(clang-patch
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${XSAN_PATCH_DIR}"
)

include(GNUInstallDirs)
set(CLANG_PATCH_DATADIR ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/clang-patch CACHE PATH "Installation path for translation files")

# install(TARGETS clang-patch
# LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
# COMPONENT clang-patch
# )
# install(TARGETS llvm-i18n
# LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
# COMPONENT clang-patch
# )
# install(DIRECTORY i18n
# DESTINATION ${CLANG_PATCH_DATADIR}
# COMPONENT clang-patch
# FILES_MATCHING PATTERN "*.yml"
# )
