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
add_dependencies(livepatch gcc-patch)

target_link_libraries(gcc-patch PRIVATE dl)

if(LLVM_LINK_LLVM_DYLIB)
    if(NOT DEFINED LLVM_SHLIB_SYMBOL_VERSION)
        # "Symbol version prefix for libLLVM.so and libclang-cpp.so"
        set(LLVM_SHLIB_SYMBOL_VERSION "LLVM_${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}")
    endif()

    message(STATUS "LLVM_SHLIB_SYMBOL_VERSION: ${LLVM_SHLIB_SYMBOL_VERSION}")

    # All in ONE DSO: libLLVM.so
    target_link_libraries(gcc-patch PRIVATE LLVM)
else()
    # libLLVMSupport.a (installed) or libLLVMSupport.so (self-built)
    target_link_libraries(gcc-patch PRIVATE LLVMSupport)
    # Please refer to to $LLVM_DIR/lib/cmake/llvm/LLVMExports.cmake
    # to see the imported targets of LLVM.
    get_target_property(_llvmsupport_type LLVMSupport TYPE)

    # If LLVMSupport is a shared library, user might get the error when using livepatch for gcc,
    # which relies on LLVMSupport, as follows:
    # gcc: error while loading shared libraries: libLLVMSupport.so.15:
    # cannot open shared object file: No such file or directory
    if(_llvmsupport_type STREQUAL "SHARED_LIBRARY")
        # libgcc-patch.so relies on libLLVMSupport.so, and libLLVMSupport.so relies on libLLVMDemangle.so.
        # Both of libLLVMSupport.so and libLLVMDemangle.so have the RUNPATH `$ORIGIN/../lib`.
        # Therefore, we can set the RUNPATH of libgcc-patch.so to `$ORIGIN/../lib` as well, and
        # install the LLVMSupport and LLVMDemangle to the patch/../lib directory.
        get_target_property(LLVMSupport_LOCATION LLVMSupport LOCATION)
        get_target_property(LLVMDemangle_LOCATION LLVMDemangle LOCATION)

        # You can use `readelf -d <ELF> | grep 'RPATH\|RUNPATH'` to check the RUNPATH.
        # Privilege: RPATH > LD_LIBRARIES_DIR > RUNPATH
        # The libLLVMSupport.so has the RUNPATH `$ORIGIN/../lib`,
        # When installing the patch, set `-rpath=$ORIGIN/../lib` to the patch, so that
        # the gcc's livepatch can search the LLVMSupport in the lib directory.
        set_target_properties(gcc-patch
            PROPERTIES
            # When build, we have different RPATH.
            INSTALL_RPATH "\$ORIGIN/../lib"
        )

        # # INSTALL_RPATH modifies the RPATH in install time as follows:
        # file(RPATH_CHANGE
        # FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/xsan/patch/libgcc-patch.so"
        # OLD_RPATH "/data/lib/llvm/build/lib:"
        # NEW_RPATH "$ORIGIN/../lib")

        # Install the LLVMSupport and LLVMDemangle to the patch/../lib directory.
        install(FILES ${LLVMSupport_LOCATION} DESTINATION ${XSAN_INSTALL_RUNTIME_DIR} COMPONENT patch)
        install(FILES ${LLVMDemangle_LOCATION} DESTINATION ${XSAN_INSTALL_RUNTIME_DIR} COMPONENT patch)
    endif()
endif()

set_target_properties(gcc-patch
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${XSAN_OUTPUT_PATCHDIR}"
)

install(TARGETS gcc-patch
    LIBRARY
    DESTINATION ${XSAN_INSTALL_PATCHDIR}
    COMPONENT patch
)
