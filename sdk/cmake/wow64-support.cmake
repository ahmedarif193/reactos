#
# WOW64 Dual-Architecture Build Support
# ======================================
#
# This module provides infrastructure for building both 64-bit and 32-bit binaries
# from the same source tree to support Windows-on-Windows 64-bit (WOW64) emulation.
#
# On amd64 builds, this enables:
# - 64-bit system32 binaries (native execution)
# - 32-bit SysWOW64 binaries (WOW64 emulation layer)
# - WOW64 thunking DLLs (wow64.dll, wow64cpu.dll, wow64win.dll)
#
# Usage:
#   1. Build i386 binaries separately:
#      ./configure.sh -DARCH=i386 -DCMAKE_BUILD_TYPE=Release
#      ninja
#
#   2. Build amd64 binaries with WOW64 support:
#      ./configure.sh -DARCH=amd64 -DREACTOS_I386_ROOT=/path/to/i386/output
#      ninja
#
# The build system will automatically:
# - Copy i386 binaries from system32 to SysWOW64
# - Setup dual KnownDlls registry entries
# - Include 32-bit SxS assemblies
# - Exclude developer artifacts (.pdb, .lib, .map, etc.)
#

# WOW64_CONFIGURE_PATHS()
# Searches for and validates i386 binary locations for WOW64 mirroring
function(WOW64_CONFIGURE_PATHS)
    if(NOT ARCH STREQUAL "amd64")
        return()
    endif()

    # Common search locations for i386 builds
    set(_SEARCH_PATHS
        "${REACTOS_BINARY_DIR}/../output-MinGW-i386-Release"
        "${REACTOS_BINARY_DIR}/../output-MinGW-i386-Debug"
        "${REACTOS_BINARY_DIR}/../build-i386/output-MinGW-i386-Release"
        "${REACTOS_BINARY_DIR}/../build-i386/output-MinGW-i386-Debug"
    )

    # Allow override via environment variable
    if(DEFINED ENV{REACTOS_I386_ROOT})
        list(INSERT _SEARCH_PATHS 0 "$ENV{REACTOS_I386_ROOT}")
    endif()

    # Allow override via CMake variable
    if(DEFINED REACTOS_I386_ROOT)
        list(INSERT _SEARCH_PATHS 0 "${REACTOS_I386_ROOT}")
    endif()

    # Search for valid i386 build
    set(_I386_ROOT "")
    foreach(_path IN LISTS _SEARCH_PATHS)
        if(EXISTS "${_path}/reactos/system32/ntdll.dll")
            set(_I386_ROOT "${_path}/reactos")
            message(STATUS "WOW64: Using i386 binaries from: ${_I386_ROOT}")
            break()
        endif()
    endforeach()

    if(NOT _I386_ROOT)
        message(STATUS "WOW64: No i386 binaries found.")
        message(STATUS "       To enable 32-bit execution on amd64:")
        message(STATUS "       1. Build i386 binaries separately")
        message(STATUS "       2. Set REACTOS_I386_ROOT to the i386 output directory")
        message(STATUS "       3. Re-run CMake configuration")
    endif()

    # Export to parent scope
    set(WOW64_I386_ROOT "${_I386_ROOT}" PARENT_SCOPE)
    set(WOW64_ENABLED "$<BOOL:${_I386_ROOT}>" PARENT_SCOPE)
endfunction()

# WOW64_ADD_32BIT_MODULE(target_name)
# Marks a module to be built as both 64-bit (system32) and 32-bit (SysWOW64)
# This is a placeholder for future cross-compilation support
function(WOW64_ADD_32BIT_MODULE _target)
    if(NOT ARCH STREQUAL "amd64")
        return()
    endif()

    # Currently, we rely on external i386 builds
    # Future: Support cross-compilation using -m32 or separate toolchain
    # set_target_properties(${_target} PROPERTIES
    #     WOW64_32BIT_BUILD TRUE
    #     COMPILE_OPTIONS "-m32"
    #     LINK_OPTIONS "-m32")
endfunction()

# WOW64_MIRROR_BINARIES()
# Populates SysWOW64 directory with 32-bit binaries from i386 build
# Called during image creation phase (boot_images.cmake)
function(WOW64_MIRROR_BINARIES _image_list_file _i386_root)
    if(NOT ARCH STREQUAL "amd64")
        return()
    endif()

    if(NOT EXISTS "${_i386_root}/system32")
        message(WARNING "WOW64: i386 root does not contain system32: ${_i386_root}")
        return()
    endif()

    # Developer artifacts to exclude
    set(_SKIP_EXTENSIONS
        ".a" ".lib" ".pdb" ".exp" ".map" ".obj" ".ilk" ".idb"
        ".log" ".txt" ".cmake" ".py" ".pl" ".bat" ".sh"
        ".c" ".cpp" ".h" ".hpp" ".idl" ".inf" ".ini" ".md"
    )

    # Collect all files from i386 system32
    file(GLOB_RECURSE _FILES LIST_DIRECTORIES FALSE
        RELATIVE "${_i386_root}/system32"
        "${_i386_root}/system32/*")

    set(_mirrored_count 0)
    foreach(_rel_path IN LISTS _FILES)
        # Extract extension
        string(REGEX MATCH "\\.[^.]*$" _ext "${_rel_path}")
        string(TOLOWER "${_ext}" _ext)

        # Check if extension should be skipped
        set(_skip FALSE)
        foreach(_skip_ext IN LISTS _SKIP_EXTENSIONS)
            if(_ext STREQUAL _skip_ext)
                set(_skip TRUE)
                break()
            endif()
        endforeach()

        if(_skip)
            continue()
        endif()

        # Add to image list
        set(_src "${_i386_root}/system32/${_rel_path}")
        set(_dst "reactos/SysWOW64/${_rel_path}")
        file(APPEND "${_image_list_file}" "${_dst}=${_src}\n")
        math(EXPR _mirrored_count "${_mirrored_count} + 1")
    endforeach()

    if(_mirrored_count GREATER 0)
        message(STATUS "WOW64: Mirrored ${_mirrored_count} files to SysWOW64")
    endif()
endfunction()

# WOW64_GET_SKIP_EXTENSIONS(output_var)
# Returns list of file extensions to exclude from SysWOW64
function(WOW64_GET_SKIP_EXTENSIONS _output)
    set(${_output}
        ".a" ".lib" ".pdb" ".exp" ".map" ".obj" ".ilk" ".idb"
        ".log" ".txt" ".cmake" ".py" ".pl" ".bat" ".sh"
        ".c" ".cpp" ".h" ".hpp" ".idl" ".inf" ".ini" ".md"
        PARENT_SCOPE)
endfunction()

# Initialize WOW64 paths at configuration time
if(ARCH STREQUAL "amd64")
    WOW64_CONFIGURE_PATHS()
    if(WOW64_I386_ROOT)
        message(STATUS "WOW64: Dual-architecture build enabled")
        message(STATUS "       64-bit binaries: reactos/system32")
        message(STATUS "       32-bit binaries: reactos/SysWOW64 (from ${WOW64_I386_ROOT})")
    else()
        message(STATUS "WOW64: Single-architecture build (64-bit only)")
        message(STATUS "       32-bit executables will not be supported")
    endif()
endif()
