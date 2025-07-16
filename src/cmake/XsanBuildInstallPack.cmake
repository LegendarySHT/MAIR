# Introduce GNUInstallDirs module, automatically add install directories.
include(GNUInstallDirs)

# ------------------------ Vars  ---------------------------------
set(XSAN_REL_BIN_DIR ".")
set(XSAN_REL_LIB_DIR ".")
set(XSAN_REL_PATCH_DIR "patch")
set(XSAN_REL_DATA_DIR "${CMAKE_INSTALL_DATADIR}")
set(XSAN_REL_PASS_DIR "pass")

# ------------------------ Vars for build ------------------------
set(XSAN_BIN_DIR "${CMAKE_BINARY_DIR}/${XSAN_REL_BIN_DIR}")
set(XSAN_LIB_DIR "${CMAKE_BINARY_DIR}/${XSAN_REL_LIB_DIR}")
set(XSAN_PATCH_DIR "${CMAKE_BINARY_DIR}/${XSAN_REL_PATCH_DIR}")
set(XSAN_DATA_DIR "${CMAKE_BINARY_DIR}/${XSAN_REL_DATA_DIR}")
set(XSAN_PASS_DIR "${CMAKE_BINARY_DIR}/${XSAN_REL_PASS_DIR}")

# ------------------------ Vars for install ------------------------
# The whole XSan is installed to ${XSAN_INSTALL_DIR}, to preserve the designated directory structure.

# To disable the default installation of add_llvm_pass_plugin()
set(LLVM_INSTALL_TOOLCHAIN_ONLY ON CACHE BOOL "Only include toolchain files in the 'install' target." FORCE)

set(XSAN_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/xsan")

# To change the default value of COMPILER_RT_INSTALL_PATH
set(COMPILER_RT_INSTALL_PATH "${XSAN_INSTALL_DIR}" CACHE PATH
    "Prefix for directories where built compiler-rt artifacts should be installed."
    FORCE # Use FORCE to override the default value in base-config-ix.cmake
)
set(XSAN_INSTALL_BINDIR ${XSAN_INSTALL_DIR}/${XSAN_REL_BIN_DIR} CACHE PATH "Installation path for bin")
set(XSAN_INSTALL_LIBDIR ${XSAN_INSTALL_DIR}/${XSAN_REL_LIB_DIR} CACHE PATH "Installation path for lib")
set(XSAN_INSTALL_PATCHDIR ${XSAN_INSTALL_DIR}/${XSAN_REL_PATCH_DIR} CACHE PATH "Installation path for livepatch")
set(XSAN_INSTALL_PASSDIR ${XSAN_INSTALL_DIR}/${XSAN_REL_PASS_DIR} CACHE PATH "Installation path for pass")
set(XSAN_INSTALL_DATADIR ${XSAN_INSTALL_DIR}/${XSAN_REL_DATA_DIR} CACHE PATH "Installation path for resource")

# ## Template for install(TARGETS|FILES|DIRECTORY)
# install(TARGETS ${name}
# LIBRARY DESTINATION <Path> COMPONENT <name>
# ARCHIVE DESTINATION <Path> COMPONENT <name>
# RUNTIME DESTINATION <Path> COMPONENT <name>
# )
# install(FILES xxx
# DESTINATION ...
# COMPONENT ...
# PERMISSIONS # 755 permission: rwxr-xr-x
# OWNER_READ OWNER_WRITE OWNER_EXECUTE
# GROUP_READ GROUP_EXECUTE
# WORLD_READ WORLD_EXECUTE
# )
# install(DIRECTORY xxx
# DESTINATION ...
# COMPONENT ...
# FILES_MATCHING PATTERN "*.yml"
# )
# install(CODE "....") # code to be executed at install time

# cmake --insatll .
# cmake --install . --component patch
# -----------------------------------------------------------------------

# ------------------------ Setting for packaging ------------------------

# CPack Setting -- MetaInfo & Format
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_NAME "XSan" CACHE STRING "The name of the package.")
set(CPACK_PACKAGE_VENDOR "Camsyn" CACHE STRING "The vendor of the package.")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "XSan is a project aiming to compose a set of sanitizers in a efficient and scalable way. "
    "The project is based on the LLVM/Clang compiler infrastructure and is implemented as a Clang/GCC plugin."
    CACHE STRING "The summary of the package.")

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
# set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md" CACHE STRING "The README file of the package.")

# Specify the archive
set(CPACK_GENERATOR "TGZ;ZIP" CACHE STRING "The generator of the package.")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP" CACHE STRING "The generator of the source package.")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/;/.git/;/.github/;/.vscode/;/;~$" CACHE STRING "The ignore files of the source package.")

# Introduce CPack module, automatically add make/package target via install() resolving.
# We can run one of the following commands to generate the package:
# - make package
# - make package_source
# Or use `cpack` / `cmake --build . --target package` to generate the package.
include(CPack)

# TODO: support package in different platforms
