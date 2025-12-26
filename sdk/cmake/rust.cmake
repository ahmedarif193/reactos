# ReactOS Rust integration helpers (Phase 1)

include_guard(GLOBAL)

# Find cargo/rustc/rustup
find_program(ROS_CARGO_EXECUTABLE NAMES cargo)
find_program(ROS_RUSTC_EXECUTABLE NAMES rustc)
find_program(ROS_RUSTUP_EXECUTABLE NAMES rustup)

if(ROS_CARGO_EXECUTABLE AND ROS_RUSTC_EXECUTABLE)
    set(ROS_RUST_FOUND TRUE)
else()
    set(ROS_RUST_FOUND FALSE)
endif()

# Verify the toolchain is usable (rustup default set, etc.)
if(ROS_RUST_FOUND)
    set(_ros_rust_toolchain_ok TRUE)
    set(_ros_rust_missing_default FALSE)

    execute_process(
        COMMAND ${ROS_RUSTC_EXECUTABLE} --version
        RESULT_VARIABLE _ros_rustc_ver_rv
        OUTPUT_VARIABLE _ros_rustc_v
        ERROR_VARIABLE  _ros_rustc_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _ros_rustc_ver_rv EQUAL 0)
        if(_ros_rustc_err MATCHES "no default is configured")
            set(_ros_rust_missing_default TRUE)
        else()
            set(_ros_rust_toolchain_ok FALSE)
        endif()
    endif()

    execute_process(
        COMMAND ${ROS_CARGO_EXECUTABLE} --version
        RESULT_VARIABLE _ros_cargo_ver_rv
        OUTPUT_VARIABLE _ros_cargo_v
        ERROR_VARIABLE  _ros_cargo_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _ros_cargo_ver_rv EQUAL 0)
        if(_ros_cargo_err MATCHES "no default is configured")
            set(_ros_rust_missing_default TRUE)
        else()
            set(_ros_rust_toolchain_ok FALSE)
        endif()
    endif()

    if(NOT _ros_rust_toolchain_ok)
        message(WARNING "Rust toolchain not usable; disabling Rust modules. Configure a default toolchain (e.g., 'rustup default stable') to enable.\n rustc: ${_ros_rustc_err}\n cargo: ${_ros_cargo_err}")
        set(ROS_RUST_FOUND FALSE)
    elseif(_ros_rust_missing_default)
        message(STATUS "Rust toolchain proxies detected without default; relying on per-target RUSTUP_TOOLCHAIN assignments.")
    endif()
endif()

# Map ReactOS ARCH to a Rust target triple
function(ros_rust_target_from_arch _arch _out_var)
    string(TOLOWER "${_arch}" _arch_lc)
    if(_arch_lc STREQUAL "i386")
        set(_triple "i686-pc-windows-gnu")
    elseif(_arch_lc STREQUAL "amd64")
        set(_triple "x86_64-pc-windows-gnu")
    else()
        set(_triple "")
    endif()
    set(${_out_var} "${_triple}" PARENT_SCOPE)
endfunction()

# Map Rust windows-gnu target triple to Clang's MinGW triplet form
function(ros_clang_mingw_triplet_from_rust _rust_triple _out_var)
    if("${_rust_triple}" STREQUAL "i686-pc-windows-gnu")
        set(${_out_var} "i686-w64-mingw32" PARENT_SCOPE)
    elseif("${_rust_triple}" STREQUAL "x86_64-pc-windows-gnu")
        set(${_out_var} "x86_64-w64-mingw32" PARENT_SCOPE)
    else()
        set(${_out_var} "" PARENT_SCOPE)
    endif()
endfunction()

# Compute default target triple for current build ARCH
if(NOT DEFINED ROS_RUST_TARGET_TRIPLE)
    if(DEFINED ARCH)
        ros_rust_target_from_arch(${ARCH} _ros_triple)
        set(ROS_RUST_TARGET_TRIPLE "${_ros_triple}")
    else()
        set(ROS_RUST_TARGET_TRIPLE "")
    endif()
endif()

# Auto-install missing rust target if requested and rustup is present
option(ROS_RUST_AUTO_INSTALL_TARGETS "Automatically install missing Rust targets via rustup" ON)

# Check rustup installed targets if possible
set(ROS_RUST_TARGET_INSTALLED UNKNOWN)
if(ROS_RUST_FOUND AND ROS_RUSTUP_EXECUTABLE AND ROS_RUST_TARGET_TRIPLE)
    execute_process(
        COMMAND ${ROS_RUSTUP_EXECUTABLE} target list --installed
        RESULT_VARIABLE _ros_rustup_list_rv
        OUTPUT_VARIABLE _ros_rustup_targets
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE _ros_rustup_list_err
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(_ros_rustup_list_rv EQUAL 0)
        if("${_ros_rustup_targets}" MATCHES "(^|\n)${ROS_RUST_TARGET_TRIPLE}(\n|$)")
            set(ROS_RUST_TARGET_INSTALLED TRUE)
        else()
            set(ROS_RUST_TARGET_INSTALLED FALSE)
            if(ROS_RUST_AUTO_INSTALL_TARGETS)
                message(STATUS "Rust target '${ROS_RUST_TARGET_TRIPLE}' not installed; attempting rustup installation...")
                execute_process(
                    COMMAND ${ROS_RUSTUP_EXECUTABLE} target add ${ROS_RUST_TARGET_TRIPLE}
                    RESULT_VARIABLE _ros_rustup_add_rv
                    OUTPUT_VARIABLE _ros_rustup_add_out
                    ERROR_VARIABLE  _ros_rustup_add_err
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE
                )
                if(_ros_rustup_add_rv EQUAL 0)
                    message(STATUS "Installed Rust target '${ROS_RUST_TARGET_TRIPLE}'")
                    set(ROS_RUST_TARGET_INSTALLED TRUE)
                else()
                    message(WARNING "Failed to install Rust target '${ROS_RUST_TARGET_TRIPLE}': ${_ros_rustup_add_err}")
                endif()
            endif()
        endif()
    else()
        if("${_ros_rustup_list_err}" MATCHES "no default is configured")
            set(ROS_RUST_TARGET_INSTALLED UNKNOWN)
            message(STATUS "rustup has no default toolchain; will rely on per-target --toolchain directives.")
        else()
            # rustup present but not configured; warn and continue with Rust disabled
            set(ROS_RUST_TARGET_INSTALLED UNKNOWN)
            message(WARNING "rustup target enumeration failed; disabling Rust modules. ${_ros_rustup_list_err}")
            set(ROS_RUST_FOUND FALSE)
        endif()
    endif()
endif()

# Defaults for cross-linking via MinGW
set(ROS_RUST_LINKER "${CMAKE_C_COMPILER}")
set(ROS_RUST_AR "${CMAKE_AR}")

# If using Clang toolchain for C/C++, prefer MinGW GCC driver for Rust linking
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Derive MinGW triplet from the current ReactOS ARCH
    ros_rust_target_from_arch(${ARCH} _ros_rust_triple_for_link)
    ros_clang_mingw_triplet_from_rust(${_ros_rust_triple_for_link} _mingw_triplet_for_link)
    if(_mingw_triplet_for_link)
        set(_ros_rust_linker_name "${_mingw_triplet_for_link}-gcc")
        set(_ros_rust_ar_name "${_mingw_triplet_for_link}-ar")

        # Re-use the same binutils directory Clang is already using (from -fuse-ld)
        set(_ros_rust_tool_dirs)
        if(CMAKE_EXE_LINKER_FLAGS MATCHES "-fuse-ld=([^ \"']+)")
            get_filename_component(_ros_ld_dir "${CMAKE_MATCH_1}" DIRECTORY)
            if(NOT _ros_ld_dir STREQUAL "" AND NOT _ros_ld_dir STREQUAL ".")
                list(APPEND _ros_rust_tool_dirs "${_ros_ld_dir}")
            endif()
        endif()

        foreach(_ros_tool_var IN ITEMS CMAKE_ASM_COMPILER CMAKE_RC_COMPILER CMAKE_DLLTOOL)
            if(DEFINED ${_ros_tool_var} AND NOT "${${_ros_tool_var}}" STREQUAL "")
                get_filename_component(_ros_tool_dir "${${_ros_tool_var}}" DIRECTORY)
                if(NOT _ros_tool_dir STREQUAL "" AND NOT _ros_tool_dir STREQUAL ".")
                    list(APPEND _ros_rust_tool_dirs "${_ros_tool_dir}")
                endif()
            endif()
        endforeach()

        set(_ros_rust_hint_args)
        if(_ros_rust_tool_dirs)
            list(REMOVE_DUPLICATES _ros_rust_tool_dirs)
            list(APPEND _ros_rust_hint_args HINTS ${_ros_rust_tool_dirs})
        endif()

        find_program(_ros_rust_linker_path NAMES "${_ros_rust_linker_name}" ${_ros_rust_hint_args} NO_CACHE)
        if(_ros_rust_linker_path)
            set(ROS_RUST_LINKER "${_ros_rust_linker_path}")
        else()
            set(ROS_RUST_LINKER "${_ros_rust_linker_name}")
        endif()

        find_program(_ros_rust_ar_path NAMES "${_ros_rust_ar_name}" ${_ros_rust_hint_args} NO_CACHE)
        if(_ros_rust_ar_path)
            set(ROS_RUST_AR "${_ros_rust_ar_path}")
        else()
            set(ROS_RUST_AR "${_ros_rust_ar_name}")
        endif()

        if(DEFINED ROS_GNU_MINGW_TOOLCHAIN_PATH AND NOT "${ROS_GNU_MINGW_TOOLCHAIN_PATH}" STREQUAL "")
            set(_ros_gnu_linker "${ROS_GNU_MINGW_TOOLCHAIN_PATH}/${_ros_rust_linker_name}")
            set(_ros_gnu_ar "${ROS_GNU_MINGW_TOOLCHAIN_PATH}/${_ros_rust_ar_name}")
            if(EXISTS "${_ros_gnu_linker}")
                set(ROS_RUST_LINKER "${_ros_gnu_linker}")
            endif()
            if(EXISTS "${_ros_gnu_ar}")
                set(ROS_RUST_AR "${_ros_gnu_ar}")
            endif()
            unset(_ros_gnu_linker)
            unset(_ros_gnu_ar)
        endif()

        unset(_ros_rust_tool_dirs)
        unset(_ros_ld_dir)
        unset(_ros_tool_dir)
        unset(_ros_rust_hint_args)
        unset(_ros_rust_linker_path)
        unset(_ros_rust_ar_path)
    endif()
endif()

# Provide a ready-to-use env list for `cmake -E env` in custom commands
function(ros_rust_make_env _out_var)
    if(NOT ROS_RUST_FOUND)
        set(${_out_var} "" PARENT_SCOPE)
        return()
    endif()

    if(NOT ROS_RUST_TARGET_TRIPLE)
        set(${_out_var} "" PARENT_SCOPE)
        return()
    endif()

    set(_env_list
        "CC_${ROS_RUST_TARGET_TRIPLE}=${ROS_RUST_LINKER}"
        "AR_${ROS_RUST_TARGET_TRIPLE}=${ROS_RUST_AR}"
        "RUSTFLAGS=-C linker=${ROS_RUST_LINKER}"
    )
    set(${_out_var} "${_env_list}" PARENT_SCOPE)
endfunction()

# Whether Rust is enabled for the build (controlled by presence and option)
option(ENABLE_RUST "Enable building Rust-based modules" ON)
option(ROS_RUST_USE_RSYM "Use rsym on Rust binaries (may fail due to long symbols)" OFF)
set(ROS_RUST_ENABLED FALSE)
if(ENABLE_RUST AND ROS_RUST_FOUND AND ROS_RUST_TARGET_TRIPLE)
    set(ROS_RUST_ENABLED TRUE)
endif()

# Helper: map CMake build configuration to Cargo profile directory name
function(ros_rust_profile_dir _out_var)
    set(_profile_dir debug)
    if(CMAKE_CONFIGURATION_TYPES)
        # Multi-config generators: default to release for size unless overridden
        if(DEFINED ROS_RUST_DEFAULT_PROFILE AND NOT ROS_RUST_DEFAULT_PROFILE STREQUAL "")
            string(TOLOWER "${ROS_RUST_DEFAULT_PROFILE}" _p)
            if(_p STREQUAL "release")
                set(_profile_dir release)
            endif()
        else()
            set(_profile_dir release)
        endif()
    else()
        string(TOLOWER "${CMAKE_BUILD_TYPE}" _bt)
        if(NOT _bt STREQUAL "debug")
            # Treat any non-Debug configuration (RelWithDebInfo, MinSizeRel, Release, etc.) as release
            set(_profile_dir release)
        endif()
    endif()
    set(${_out_var} "${_profile_dir}" PARENT_SCOPE)
endfunction()

# Public API: add a Rust executable built by cargo and optionally package it
# Usage:
#   ros_add_rust_executable(<target>
#       [CRATE_DIR <path>]
#       [BINARY_NAME <name>]
#       [TARGET_TRIPLE <triple>]
#       [FEATURES f1;f2;...]
#       [PROFILE debug|release]
#       [ENV key=val;key=val]
#       [DESTINATION <cd-path>]
#       [NAME_ON_CD <name.exe>]
#       [CD_FOR <list>]
#       [NO_CAB]
#       [SKIP_IF_MISSING])
function(ros_add_rust_executable _target)
    cmake_parse_arguments(_RUST "SKIP_IF_MISSING;NO_CAB" "CRATE_DIR;BINARY_NAME;TARGET_TRIPLE;PROFILE;DESTINATION;NAME_ON_CD" "FEATURES;ENV;CD_FOR" ${ARGN})

    if(NOT ROS_RUST_ENABLED)
        if(_RUST_SKIP_IF_MISSING)
            message(STATUS "${_target}: Rust disabled or not found; skipping target (SKIP_IF_MISSING)")
            return()
        else()
            message(STATUS "${_target}: Rust disabled or not found; skipping target")
            return()
        endif()
    endif()

    # Ensure the Rust target is installed at build time; create a stamp
    set(_install_target_stamp)
    set(_rustup_toolchain_override "")
    if(_RUST_ENV)
        foreach(_pair ${_RUST_ENV})
            if(_pair MATCHES "^RUSTUP_TOOLCHAIN=(.+)$")
                set(_rustup_toolchain_override "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()

    if(ROS_RUSTUP_EXECUTABLE AND ROS_RUST_TARGET_TRIPLE)
        set(_rustup_toolchain_args)
        if(NOT _rustup_toolchain_override STREQUAL "")
            set(_rustup_toolchain_args --toolchain ${_rustup_toolchain_override})
        endif()
        set(_install_target_stamp ${CMAKE_CURRENT_BINARY_DIR}/cargo-${_target}/.${ROS_RUST_TARGET_TRIPLE}.installed.stamp)
        add_custom_command(
            OUTPUT ${_install_target_stamp}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/cargo-${_target}
            COMMAND ${ROS_RUSTUP_EXECUTABLE} target add ${_rustup_toolchain_args} ${ROS_RUST_TARGET_TRIPLE}
            COMMAND ${CMAKE_COMMAND} -E touch ${_install_target_stamp}
            VERBATIM
            COMMENT "Ensuring Rust target '${ROS_RUST_TARGET_TRIPLE}' is installed"
            USES_TERMINAL
        )
    endif()

    set(_crate_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    if(_RUST_CRATE_DIR)
        set(_crate_dir "${_RUST_CRATE_DIR}")
    endif()

    set(_bin_name "${_target}")
    if(_RUST_BINARY_NAME)
        set(_bin_name "${_RUST_BINARY_NAME}")
    endif()

    set(_triple "${ROS_RUST_TARGET_TRIPLE}")
    if(_RUST_TARGET_TRIPLE)
        set(_triple "${_RUST_TARGET_TRIPLE}")
    endif()

    if(NOT _triple)
        message(WARNING "${_target}: No Rust target triple for ARCH='${ARCH}', skipping")
        return()
    endif()

    # Determine cargo profile
    if(_RUST_PROFILE)
        string(TOLOWER "${_RUST_PROFILE}" _profile_dir)
    else()
        ros_rust_profile_dir(_profile_dir)
    endif()
    if(_profile_dir STREQUAL "release")
        set(_cargo_profile_flag --release)
    else()
        set(_cargo_profile_flag)
    endif()

    # ENV base list for cmake -E env
    ros_rust_make_env(_base_env)

    # Determine in-tree importlib directories to prefer over system MinGW ones
    set(_importlib_targets libkernel32 libmsvcrt libntdll libuser32 libadvapi32 libshell32 libgdi32)
    set(_importlib_dirs)
    foreach(_lib ${_importlib_targets})
        if(TARGET ${_lib})
            list(APPEND _importlib_dirs $<TARGET_FILE_DIR:${_lib}>)
        endif()
    endforeach()
    # Fallback: also add common importlib directories explicitly so we prefer in-tree libs
    list(APPEND _importlib_dirs
        ${REACTOS_BINARY_DIR}/dll/win32/kernel32
        ${REACTOS_BINARY_DIR}/dll/ntdll
        ${REACTOS_BINARY_DIR}/dll/win32/user32
        ${REACTOS_BINARY_DIR}/dll/win32/advapi32
        ${REACTOS_BINARY_DIR}/dll/win32/shell32
        ${REACTOS_BINARY_DIR}/dll/win32/gdi32)
    list(REMOVE_DUPLICATES _importlib_dirs)

    # Build RUSTFLAGS by extending the base '-C linker=...'
    set(_rustflags_base "-C linker=${ROS_RUST_LINKER}")
    set(_rust_native_flags "")
    foreach(_dir ${_importlib_dirs})
        set(_rust_native_flags "${_rust_native_flags} -L native=${_dir}")
    endforeach()
    # Add target-specific linker args
    set(_rust_target_extra "")
    # If we are actually using clang as the Rust linker, add the target so it
    # selects the MinGW cross toolchain instead of the host linker.
    if(ROS_RUST_LINKER STREQUAL "${CMAKE_C_COMPILER}" AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
        ros_clang_mingw_triplet_from_rust(${_triple} _clang_triplet)
        if(_clang_triplet)
            set(_rust_target_extra "${_rust_target_extra} -C link-arg=--target=${_clang_triplet}")
        endif()
    endif()
    if(_triple STREQUAL "i686-pc-windows-gnu")
        # 32-bit MinGW: prefer abort-on-panic to avoid unwinder dependency.
        set(_rust_target_extra "${_rust_target_extra} -C panic=abort")
        # Win8+ sync primitives (WaitOnAddress/WakeByAddress*) live in Synchronization.lib
        # which is not re-exported by kernel32 on i686, so link it explicitly.
        set(_rust_target_extra "${_rust_target_extra} -C link-arg=-lsynchronization")

        # Inject a tiny PE/COFF object that satisfies _Unwind_Resume so we do
        # not need to drag in libgcc_s when building with Rust on i686.
        set(_shim_c ${CMAKE_CURRENT_BINARY_DIR}/${_target}-unwind_shim32.c)
        set(_shim_o ${CMAKE_CURRENT_BINARY_DIR}/${_target}-unwind_shim32.o)
        file(WRITE ${_shim_c}
"#include <stdlib.h>

__attribute__((noreturn))
void _Unwind_Resume(void *state)
{
    (void)state;
    abort();
}
")
        add_custom_command(
            OUTPUT ${_shim_o}
            COMMAND ${ROS_RUST_LINKER} -x c -c -O2 -o ${_shim_o} ${_shim_c}
            DEPENDS ${_shim_c}
            VERBATIM
            COMMENT "Compiling i686 unwind shim (PE/COFF)"
        )
        set(_extra_link_objs ${_shim_o})
        set(_obj_link_args "-C link-arg=${_shim_o}")
    endif()

    # Optional extra link objects (e.g., unwind shims for i686)
    if(NOT DEFINED _obj_link_args)
        set(_obj_link_args "")
    endif()

    string(STRIP "${_rustflags_base} ${_rust_native_flags} ${_rust_target_extra} ${_obj_link_args}" _rustflags_full)

    # Construct LIBRARY_PATH for GCC linker precedence (optional)
    set(_library_path "")
    foreach(_dir ${_importlib_dirs})
        if(_library_path STREQUAL "")
            set(_library_path "${_dir}")
        else()
            set(_library_path "${_library_path}:${_dir}")
        endif()
    endforeach()

    # Merge env: replace any RUSTFLAGS entry with our extended flags
    set(_env)
    foreach(_pair ${_base_env})
        if(_pair MATCHES "^RUSTFLAGS=")
            # skip, we will insert our own below
        else()
            list(APPEND _env "${_pair}")
        endif()
    endforeach()
    list(APPEND _env "RUSTFLAGS=${_rustflags_full}")
    if(NOT _library_path STREQUAL "")
        list(APPEND _env "LIBRARY_PATH=${_library_path}")
    endif()
    # User-provided extra env
    foreach(_pair ${_RUST_ENV})
        if(_pair MATCHES "^RUSTUP_TOOLCHAIN=")
            continue()
        endif()
        list(APPEND _env "${_pair}")
    endforeach()
    if(NOT _rustup_toolchain_override STREQUAL "")
        list(APPEND _env "RUSTUP_TOOLCHAIN=${_rustup_toolchain_override}")
    endif()

    # Features handling
    set(_feature_args)
    if(_RUST_FEATURES)
        string(REPLACE ";" "," _feature_list "${_RUST_FEATURES}")
        list(APPEND _feature_args --features ${_feature_list})
    endif()

    # Per-target cargo target directory to avoid conflicts
    set(_cargo_target_dir ${CMAKE_CURRENT_BINARY_DIR}/cargo-${_target})
    file(MAKE_DIRECTORY ${_cargo_target_dir})

    set(_out_exe ${_cargo_target_dir}/${_triple}/${_profile_dir}/${_bin_name}.exe)

    # Track crate sources to ensure rebuild when .rs files or Cargo manifest change
    # Track all Rust sources under the crate directory (any subfolders)
    file(GLOB_RECURSE _crate_rs CONFIGURE_DEPENDS
        "${_crate_dir}/*.rs")
    # Track optional build script explicitly
    if(EXISTS "${_crate_dir}/build.rs")
        list(APPEND _crate_rs "${_crate_dir}/build.rs")
    endif()
    set(_crate_manifest "${_crate_dir}/Cargo.toml")
    set(_crate_lock     "${_crate_dir}/Cargo.lock")

    # Cargo emits a depfile (<bin>.d) that Ninja can use for finer-grained tracking
    set(_depfile "${_cargo_target_dir}/${_triple}/${_profile_dir}/${_bin_name}.d")

    # Build depfile argument only for Ninja
    set(_depfile_arg)
    if(CMAKE_GENERATOR MATCHES "Ninja")
        set(_depfile_arg DEPFILE ${_depfile})
    endif()

    add_custom_command(
        OUTPUT ${_out_exe}
        COMMAND ${CMAKE_COMMAND} -E env
                CARGO_TARGET_DIR=${_cargo_target_dir}
                ${_env}
                ${ROS_CARGO_EXECUTABLE} build --target ${_triple} ${_cargo_profile_flag} --bin ${_bin_name}
        WORKING_DIRECTORY ${_crate_dir}
        VERBATIM
        COMMENT "Building Rust crate '${_bin_name}' for ${_triple} (${_profile_dir})"
        USES_TERMINAL
        DEPENDS ${_install_target_stamp} ${_extra_link_objs} ${_crate_manifest} ${_crate_lock} ${_crate_rs}
        ${_depfile_arg}
    )

    # Optional debug symbols parity: separate debug or rsym processing
    set(_final_dep ${_out_exe})
    set(_need_rsym FALSE)
    if(DEFINED SEPARATE_DBG AND SEPARATE_DBG)
        # Create a stamp after debug split/strip to ensure packaging waits on this
        set(_dbg_stamp ${_cargo_target_dir}/${_bin_name}.${_triple}.${_profile_dir}.dbgstamp)

        # Try to locate rsym if available (same logic as gcc.cmake)
        set(_have_rsym FALSE)
        if(ROS_RUST_USE_RSYM)
            if(NOT DEFINED NO_ROSSYM OR (DEFINED NO_ROSSYM AND NOT NO_ROSSYM))
                get_target_property(_RSYM native-rsym IMPORTED_LOCATION)
                if(_RSYM)
                    set(_have_rsym TRUE)
                endif()
            endif()
        endif()

        # Choose rsym vs strip at configure time to avoid generator expressions
        if(_have_rsym)
            set(_split_cmd ${_RSYM} -s ${REACTOS_SOURCE_DIR} ${_out_exe} ${_out_exe})
        else()
            set(_split_cmd ${CMAKE_STRIP} --strip-debug ${_out_exe})
        endif()

        add_custom_command(
            OUTPUT ${_dbg_stamp}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${REACTOS_BINARY_DIR}/symbols
            COMMAND ${CMAKE_STRIP} --only-keep-debug ${_out_exe} -o ${REACTOS_BINARY_DIR}/symbols/${_bin_name}.exe
            COMMAND ${CMAKE_COMMAND} -E echo "split debug for ${_bin_name}" > ${_dbg_stamp}
            COMMAND ${CMAKE_COMMAND} -E rm -f ${_dbg_stamp}
            COMMAND ${_split_cmd}
            COMMAND ${CMAKE_COMMAND} -E touch ${_dbg_stamp}
            DEPENDS ${_out_exe}
            VERBATIM
            COMMENT "Splitting debug symbols for ${_bin_name} (${_profile_dir})"
            USES_TERMINAL
        )
        set(_final_dep ${_dbg_stamp})
        if(_have_rsym)
            set(_need_rsym TRUE)
        endif()
    elseif(DEFINED NO_ROSSYM AND NOT NO_ROSSYM)
        # Normal rsym build: run rsym if available
        if(ROS_RUST_USE_RSYM)
            get_target_property(_RSYM native-rsym IMPORTED_LOCATION)
            if(_RSYM)
                set(_rsym_stamp ${_cargo_target_dir}/${_bin_name}.${_triple}.${_profile_dir}.rsymstamp)
                add_custom_command(
                    OUTPUT ${_rsym_stamp}
                    COMMAND ${_RSYM} -s ${REACTOS_SOURCE_DIR} ${_out_exe} ${_out_exe}
                    COMMAND ${CMAKE_COMMAND} -E touch ${_rsym_stamp}
                    DEPENDS ${_out_exe}
                    VERBATIM
                    COMMENT "Processing rsym for ${_bin_name} (${_profile_dir})"
                    USES_TERMINAL
                )
                set(_final_dep ${_rsym_stamp})
                set(_need_rsym TRUE)
            endif()
        else()
            # Skip rsym for Rust; strip debug only
            set(_strip_stamp ${_cargo_target_dir}/${_bin_name}.${_triple}.${_profile_dir}.stripstamp)
            add_custom_command(
                OUTPUT ${_strip_stamp}
                COMMAND ${CMAKE_STRIP} --strip-debug ${_out_exe}
                COMMAND ${CMAKE_COMMAND} -E touch ${_strip_stamp}
                DEPENDS ${_out_exe}
                VERBATIM
                COMMENT "Stripping debug symbols for ${_bin_name} (${_profile_dir})"
                USES_TERMINAL
            )
            set(_final_dep ${_strip_stamp})
        endif()
    endif()

    add_custom_target(${_target} ALL DEPENDS ${_final_dep})
    # Ensure our import libraries are built before we link Rust crates
    set(_dep_imps)
    foreach(_lib ${_importlib_targets})
        if(TARGET ${_lib})
            list(APPEND _dep_imps ${_lib})
        endif()
    endforeach()
    if(_dep_imps)
        add_dependencies(${_target} ${_dep_imps})
    endif()
    if(_need_rsym AND TARGET native-rsym)
        add_dependencies(${_target} native-rsym)
    endif()

    # Optional packaging into ISO
    if(_RUST_DESTINATION)
        set(_name_on_cd ${_bin_name}.exe)
        if(_RUST_NAME_ON_CD)
            set(_name_on_cd ${_RUST_NAME_ON_CD})
        endif()

        set(_for all)
        if(_RUST_CD_FOR)
            set(_for ${_RUST_CD_FOR})
        endif()

        if(_RUST_NO_CAB)
            add_cd_file(FILE ${_out_exe} NAME_ON_CD ${_name_on_cd} DESTINATION ${_RUST_DESTINATION} FOR ${_for} NO_CAB)
        else()
            add_cd_file(FILE ${_out_exe} NAME_ON_CD ${_name_on_cd} DESTINATION ${_RUST_DESTINATION} FOR ${_for})
        endif()

        # Ensure the image/ISO aggregators also depend on the rust target so
        # that symbol processing completes before packaging
        if(TARGET reactos_cab_inf)
            add_dependencies(reactos_cab_inf ${_target})
        endif()
        if(TARGET liveimg_deps)
            add_dependencies(liveimg_deps ${_target})
        endif()
        if(TARGET bootcd)
            add_dependencies(bootcd ${_target})
        endif()
        if(TARGET livecd)
            add_dependencies(livecd ${_target})
        endif()
    endif()
endfunction()

# Friendly status messages to help users
if(ROS_RUST_FOUND)
    # Version strings computed above when verifying usability
    message(STATUS "Rust detected: ${_ros_rustc_v}; ${_ros_cargo_v}")
    if(ROS_RUST_TARGET_TRIPLE)
        if(ROS_RUST_TARGET_INSTALLED STREQUAL "FALSE")
            message(WARNING "Rust target '${ROS_RUST_TARGET_TRIPLE}' not installed. Install with: rustup target add ${ROS_RUST_TARGET_TRIPLE}")
        elseif(ROS_RUST_TARGET_INSTALLED STREQUAL "TRUE")
            message(STATUS "Rust target '${ROS_RUST_TARGET_TRIPLE}' is installed")
        endif()
    endif()
else()
    message(STATUS "Rust not found; disabling Rust-based modules for this build.")
endif()
