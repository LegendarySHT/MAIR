# Unlike clang, gcc does not expose its functionality on a library.
# That is to say, we should intercept the non-dynamic functions.

# We should intercept the following functions theoretically:
# - `error_at`: to suppress the error when enable 'incompatible' sanitizer.
# - `finish_options`: the caller of `error_at`, should not be intercepted.
# - `do_spec` : A driver function to run the spec command.
# - It is hard to intercept, because it is stripped.
# - As a result, we rewrite the link_command_spec used in `do_spec` to our own.

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# To shared libaray
add_library(gcc-patch MODULE
    diagnostic.cpp
    gcc.cpp
    ../utils/PatchHelper.cpp
)

target_link_libraries(gcc-patch PRIVATE dl)

if(LLVM_LINK_LLVM_DYLIB)
    if(NOT DEFINED LLVM_SHLIB_SYMBOL_VERSION)
        # "Symbol version prefix for libLLVM.so and libclang-cpp.so"
        set(LLVM_SHLIB_SYMBOL_VERSION "LLVM_${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}")
    endif()

    message(STATUS "LLVM_SHLIB_SYMBOL_VERSION: ${LLVM_SHLIB_SYMBOL_VERSION}")
    target_link_libraries(gcc-patch PRIVATE LLVM)
else()
    target_link_libraries(gcc-patch PRIVATE LLVMSupport)
endif()

set_target_properties(gcc-patch
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${XSAN_PATCH_DIR}"
)

include(GNUInstallDirs)
set(GCC_PATCH_DATADIR ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/gcc-patch CACHE PATH "Installation path for translation files")

# install(TARGETS gcc-patch
# LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
# COMPONENT gcc-patch
# )

# configure_file(
# ${CMAKE_CURRENT_SOURCE_DIR}/support_dso_injection.sh
# ${XSAN_PATCH_DIR}/support_dso_injection.sh
# @ONLY
# )
