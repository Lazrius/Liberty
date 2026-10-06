# Native Linux clang-cl -> Windows x86/MSVC
# Produces DWARF debug info so that Linux GDB/LLDB can provide source-level debugging for Windows binaries under Wine.

if (NOT ${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Linux")
    message(FATAL_ERROR "Linux ClangCL toolchain should only be run on Linux")
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/../Utils/GenerateLinuxIncludes.cmake")

# Cannot use CMAKE_BINARY_DIR as any other toolchain (like Conan or vcpkg) might change that when
# building dependencies. Calculate relative to where we currently are.
set(LIBERTY_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/../../build")
set(LIBERTY_CLANG_DIR "${LIBERTY_BUILD_DIR}/clang")

set(LIBERTY_CLANGCL_VFS "${LIBERTY_CLANG_DIR}/vfs.yml")
set(LIBERTY_CLANGCL_RC "${LIBERTY_CLANG_DIR}/rc")

# Calculate Linux msvc installation and generate needed clang virtual file system
set_msvc_root()
clang_msvc_generate(VFS_FILE ${LIBERTY_CLANGCL_VFS} RC_DIR ${LIBERTY_CLANGCL_RC})

# Technically not needed to compile, but ensures that IDEs will actually 'see' the files
include_directories(AFTER SYSTEM ${LIBERTY_CLANGCL_RC})

message(STATUS "clang-cl Linux toolchain:")
message(STATUS "MSVC root:    ${LIBERTY_CLANG_MSVC_ROOT}")
message(STATUS "SDK root:    ${LIBERTY_CLANG_MSVC_SDK_ROOT}")
message(STATUS "MSVC version: ${LIBERTY_CLANG_MSVC_VERSION}")
message(STATUS "Windows SDK:  ${LIBERTY_CLANG_MSVC_SDK_VERSION}")
message(STATUS "Target:       i686-windows-msvc")

set(_CLANGCL_MSVC_DIR "${LIBERTY_VFS_MSVC}/${LIBERTY_CLANG_MSVC_VERSION}")
set(_CLANGCL_SDK_DIR "${LIBERTY_VFS_SDK}")

# Target
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR "i686")

# clang-cl uses the i686-windows-msvc target for 32-bit Windows.
set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_BASE_NAME clang-cl)
set(CMAKE_C_COMPILER_TARGET i686-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET i686-windows-msvc)

# MSVC / Windows SDK configuration
set(_clang_common_flags
        "/vctoolsdir ${_CLANGCL_MSVC_DIR}"
        "/vctoolsversion ${LIBERTY_CLANG_MSVC_VERSION}"
        "/winsdkdir ${_CLANGCL_SDK_DIR}"
        "/winsdkversion ${LIBERTY_CLANG_MSVC_SDK_VERSION}"
        "-vfsoverlay ${LIBERTY_CLANGCL_VFS}"

        # MSVC-compatible C++ exception handling.
        "/EHs"

        # Generate DWARF rather than CodeView/PDB. This is what allows Linux for GDB/LLDB when debugging with Wine.
        "-gdwarf"

        # Extra defines to ensure that we are properly recognised as Windows
        "/D_WIN32"

        # Mark
        ""
)

string(JOIN " " _clang_common_flags_string ${_clang_common_flags})
set(CMAKE_C_FLAGS_INIT " ${CMAKE_C_FLAGS_INIT} ${_clang_common_flags_string}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_clang_common_flags_string}")

# LLD
set(_lld_flags
        "/vctoolsdir:${_CLANGCL_MSVC_DIR}"
        "/vctoolsversion:${LIBERTY_CLANG_MSVC_VERSION}"
        "/winsdkdir:${_CLANGCL_SDK_DIR}"
        "/winsdkversion:${LIBERTY_CLANG_MSVC_SDK_VERSION}"
        "/vfsoverlay:${LIBERTY_CLANGCL_VFS}"
)

string(JOIN " " _lld_flags_string ${_lld_flags})
set(CMAKE_EXE_LINKER_FLAGS_INIT " ${CMAKE_EXE_LINKER_FLAGS_INIT} ${_lld_flags_string}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT} ${_lld_flags_string}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${CMAKE_MODULE_LINKER_FLAGS_INIT} ${_lld_flags_string}")

# We also override llvm-rc to use regular rc, as llvm-rc cannot process the .rc files correctly
set(CMAKE_RC_COMPILER_INIT "${LIBERTY_MSVC_ROOT}/bin/x86/rc")
set(CMAKE_RC_COMPILER "${LIBERTY_MSVC_ROOT}/bin/x86/rc")

#[[find_program(LLVM_RC_EXE NAMES "llvm-rc")
if (NOT DEFINED LLVM_RC_EXE OR LLVM_RC_EXE STREQUAL "LLVM_RC_EXE-NOTFOUND")
    message(FATAL_ERROR "llvm-rc could not be located, ensure that it is installed and on the PATH")
endif()]]

find_program(LLVM_LINK_EXE NAMES "llvm-link")
if (NOT DEFINED LLVM_LINK_EXE OR LLVM_LINK_EXE STREQUAL "LLVM_LINK_EXE-NOTFOUND")
    message(FATAL_ERROR "llvm-link could not be located, ensure that it is installed and on the PATH")
endif()

#set(CMAKE_RC_COMPILER "${LLVM_RC_EXE}")
set(CMAKE_LINKER_TYPE "LLD")
set(CMAKE_LINKER "${LLVM_LINK_EXE}")

# Don't let CMake accidentally select the Linux compiler.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Make try_compile use the same settings.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
        ${CMAKE_TRY_COMPILE_PLATFORM_VARIABLES}
        LIBERTY_MSVC_ROOT
        LIBERTY_CLANG_MSVC_VERSION
        LIBERTY_CLANG_MSVC_SDK_VERSION
)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif ()

