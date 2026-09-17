# RISC-V NT uses the Windows C data model with ELF object intermediates.
# Keep the firmware's UEFI compilation options separate from the NT ABI.
set(CMAKE_MC_COMPILER native-windmc)
set(NO_ROSSYM TRUE)
set(PSEH_LIB pseh)
set(ARCH2 riscv64)
set(LLVM_DLLTOOL_MACHINE riscv64)
set(DECO_OPTION "-@")
set(USE_LLVM_CXXABI TRUE)
add_compile_definitions(_USE_DUMMY_PSEH=1
    "$<$<NOT:$<BOOL:$<TARGET_PROPERTY:WITH_CXX_EXCEPTIONS>>>:_ATL_NO_EXCEPTIONS=1>")

set(_RISCV_NT_COMPILE_OPTIONS
    -ffreestanding -fno-builtin -fms-extensions -fsigned-char -fno-strict-aliasing
    -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
    -ffunction-sections -fdata-sections -nostdlibinc
    "$<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:$<TARGET_PROPERTY:WITH_CXX_EXCEPTIONS>>,-fexceptions,-fno-exceptions>>"
    "$<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:$<TARGET_PROPERTY:WITH_CXX_RTTI>>,-frtti,-fno-rtti>>"
    -Wall -Wno-ignored-attributes -Wno-multichar -Wno-pragma-pack)

# Large fixed and dynamic frames must touch every 4 KiB NT guard interval.
# The driver does not enable this option for Windows RISC-V yet; request the
# existing frontend/backend inline contract explicitly, without __chkstk.
list(APPEND _RISCV_NT_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-Xclang -fstack-clash-protection>"
    "$<$<COMPILE_LANGUAGE:C,CXX>:-mstack-probe-size=4096>")

# Apply the NT compiler contract to every normal PE/COFF target. riscv_nt adds
# the ELF object-format override only to the temporary native-image path.
add_compile_options(${_RISCV_NT_COMPILE_OPTIONS})

# Use the version-matched LLVM C++ ABI and unwinder built for the native
# Windows/RISC-V SEH ABI. ReactOS continues to provide its established STL.
set(_RISCV64_LLVM_RUNTIME_DIR
    "${REACTOS_CLANG_LLVM_MINGW_ROOT}/lib/riscv64-w64-windows-gnu")
set(_RISCV64_LIBCXXABI
    "${_RISCV64_LLVM_RUNTIME_DIR}/libc++abi.a")
set(_RISCV64_LIBUNWIND
    "${_RISCV64_LLVM_RUNTIME_DIR}/libunwind.a")

foreach(_runtime_file IN ITEMS "${_RISCV64_LIBCXXABI}" "${_RISCV64_LIBUNWIND}")
    if(NOT EXISTS "${_runtime_file}")
        message(FATAL_ERROR
            "Required Windows/RISC-V LLVM runtime not found: ${_runtime_file}")
    endif()
endforeach()

add_library(libunwind STATIC IMPORTED GLOBAL)
set_target_properties(libunwind PROPERTIES
    IMPORTED_LOCATION "${_RISCV64_LIBUNWIND}")
target_link_libraries(libunwind INTERFACE libkernel32 libntdll)

add_library(libsupc++ STATIC IMPORTED GLOBAL)
set_target_properties(libsupc++ PROPERTIES
    IMPORTED_LOCATION "${_RISCV64_LIBCXXABI}")
target_link_libraries(libsupc++ INTERFACE libunwind)

add_library(cppstl INTERFACE)
target_link_libraries(cppstl INTERFACE cpprt stlport oldnames)
set_target_properties(cppstl PROPERTIES INTERFACE_WITH_CXX_STL TRUE)

# Match the normal Clang assembler recipe. RISC-V uses the integrated
# assembler directly, but it still consumes the shared preprocessed assembly
# headers and generated architecture offsets.
set(CMAKE_ASM_COMPILE_OBJECT
    "<CMAKE_ASM_COMPILER> -x assembler-with-cpp -o <OBJECT> -I${REACTOS_SOURCE_DIR}/sdk/include/asm -I${REACTOS_BINARY_DIR}/sdk/include/asm <INCLUDES> <FLAGS> <DEFINES> -D__ASM__ -c <SOURCE>")

# CMake's default windres rule omits ReactOS' localization definitions. That
# leaves RISC-V images without translated string tables (including shell32's
# desktop Name column), even when I18N_LANG is configured.
set(CMAKE_RC_COMPILE_OBJECT
    "<CMAKE_RC_COMPILER> -O coff <INCLUDES> <FLAGS> -DRC_INVOKED -D__WIN32__=1 -D__FLAT__=1 ${I18N_DEFS} <DEFINES> <SOURCE> <OBJECT>")
set(CMAKE_DEPFILE_FLAGS_RC
    "--preprocessor=\"${CMAKE_C_COMPILER}\" --preprocessor-arg=--target=${CMAKE_C_COMPILER_TARGET} --preprocessor-arg=-E --preprocessor-arg=-nostdlibinc --preprocessor-arg=-xc-header --preprocessor-arg=-MMD --preprocessor-arg=-MF --preprocessor-arg=<DEPFILE> --preprocessor-arg=-MT --preprocessor-arg=<OBJECT>")

# ReactOS creates import libraries from module specifications. Use the same
# explicit link recipes as the other Clang architectures so CMake's MinGW
# platform rules do not add an empty --out-implib argument to executables.
set(CMAKE_LINK_DEF_FILE_FLAG "")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_LINK_LIBRARY_SUFFIX "")
set(CMAKE_CREATE_WIN32_EXE "")
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
set(CMAKE_C_COMPILE_OPTIONS_PIE "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIE "")
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> -Wl,--start-group ${CMAKE_C_FLAGS} <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES> -Wl,--end-group")
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> -Wl,--start-group ${CMAKE_CXX_FLAGS} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES> -Wl,--end-group")
set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_C_COMPILER> -Wl,--start-group ${CMAKE_C_FLAGS} <CMAKE_SHARED_LIBRARY_C_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES> -Wl,--end-group")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_CXX_COMPILER> -Wl,--start-group ${CMAKE_CXX_FLAGS} <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES> -Wl,--end-group")
set(CMAKE_RC_CREATE_SHARED_LIBRARY "<CMAKE_C_COMPILER> -Wl,--start-group ${CMAKE_C_FLAGS} <CMAKE_SHARED_LIBRARY_C_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES> -Wl,--end-group")
set(CMAKE_C_CREATE_SHARED_MODULE ${CMAKE_C_CREATE_SHARED_LIBRARY})
set(CMAKE_CXX_CREATE_SHARED_MODULE ${CMAKE_CXX_CREATE_SHARED_LIBRARY})
set(CMAKE_RC_CREATE_SHARED_MODULE ${CMAKE_RC_CREATE_SHARED_LIBRARY})

add_library(riscv_nt INTERFACE)
target_compile_definitions(riscv_nt INTERFACE NTDDI_VERSION=${REACTOS_TARGET_NTDDI})
target_compile_options(riscv_nt INTERFACE --target=riscv64-w64-windows-gnu-elf)

set(CMAKE_EXE_LINKER_FLAGS "${REACTOS_CLANG_BASE_LINKER_FLAGS} -Wl,--disable-stdcall-fixup,--gc-sections")
set(CMAKE_SHARED_LINKER_FLAGS "${REACTOS_CLANG_BASE_LINKER_FLAGS} -Wl,--disable-stdcall-fixup")
set(CMAKE_MODULE_LINKER_FLAGS "${REACTOS_CLANG_BASE_LINKER_FLAGS} -Wl,--disable-stdcall-fixup")

macro(add_asm_files _target)
    set(${_target} ${ARGN})
endmacro()

# Keep the normal ReactOS module contract. These switches are consumed by the
# RISC-V COFF linker; they are not firmware or ELF subsystem substitutions.
function(set_entrypoint module entrypoint)
    if(entrypoint STREQUAL "0")
        target_link_options(${module} PRIVATE "-Wl,--entry=" "-Wl,-Xlink=/noentry" "-Wl,-Xlink=/dll")
    else()
        target_link_options(${module} PRIVATE "-Wl,-entry,${entrypoint}")
    endif()
endfunction()

function(set_subsystem module subsystem)
    target_link_options(${module} PRIVATE "-Wl,--subsystem,${subsystem}:10.00")
endfunction()

function(set_image_base module image_base)
    target_link_options(${module} PRIVATE "-Wl,--image-base,${image_base}")
endfunction()

function(add_linker_script target linker_script)
    # LLD's COFF driver does not consume GNU linker scripts.
endfunction()

function(set_module_type_toolchain module type)
    target_link_options(${module} PRIVATE
        -Wl,--major-image-version,${_NT_MAJOR}
        -Wl,--minor-image-version,0${_NT_MINOR}
        -Wl,--major-os-version,${_NT_MAJOR}
        -Wl,--minor-os-version,0${_NT_MINOR})
    target_link_libraries(${module} setjmp)
    if(type IN_LIST KERNEL_MODULE_TYPES)
        if(type STREQUAL "kmdfdriver")
            set(type "wdmdriver")
        endif()
        target_link_options(${module} PRIVATE
            -Wl,--exclude-all-symbols,-file-alignment=0x1000,-section-alignment=0x1000
            -Wl,/driver)
    endif()
endfunction()

function(add_delay_importlibs module)
    get_target_property(module_type ${module} TYPE)
    if(module_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "Cannot add delay imports to a static library")
    endif()
    foreach(lib ${ARGN})
        get_filename_component(basename "${lib}" NAME_WE)
        target_link_libraries(${module} lib${basename})
        if("${lib}" MATCHES "\\.")
            set(dllname "${lib}")
        else()
            set(dllname "${lib}.dll")
        endif()
        target_link_options(${module} PRIVATE "-Wl,--delayload,${dllname}")
    endforeach()
    target_link_libraries(${module} delayimp)
endfunction()

function(fixup_load_config target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND native-pefixup --loadconfig "$<TARGET_FILE:${target}>"
        COMMENT "Patching in LOAD_CONFIG")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        $<TARGET_PROPERTY:native-pefixup,IMPORTED_LOCATION>)
endfunction()

function(generate_import_lib libname dllname spec_file version_arg dbg_arg)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${libname}_implib.def
        COMMAND native-spec2def ${version_arg} ${dbg_arg} -n=${dllname} -a=${ARCH2} ${ARGN}
            --implib -d=${CMAKE_CURRENT_BINARY_DIR}/${libname}_implib.def
            ${CMAKE_CURRENT_SOURCE_DIR}/${spec_file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${spec_file} native-spec2def)
    set(library_private_dir ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${libname}.dir)
    add_custom_command(
        OUTPUT ${library_private_dir}/${libname}.a
        COMMAND ${CMAKE_COMMAND} -E rm -f ${library_private_dir}/${libname}.a
        COMMAND ${CMAKE_DLLTOOL} -d ${CMAKE_CURRENT_BINARY_DIR}/${libname}_implib.def -k
            -l ${libname}.a -m ${LLVM_DLLTOOL_MACHINE} -t ${libname}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${libname}_implib.def
        WORKING_DIRECTORY ${library_private_dir})
    add_custom_target(${libname}_implib_target DEPENDS ${library_private_dir}/${libname}.a)
    _add_library(${libname} STATIC IMPORTED GLOBAL)
    set_target_properties(${libname} PROPERTIES IMPORTED_LOCATION ${library_private_dir}/${libname}.a)
    add_dependencies(${libname} ${libname}_implib_target)
endfunction()

function(spec2def dllname spec_file)
    cmake_parse_arguments(spec "ADD_IMPORTLIB;NO_PRIVATE_WARNINGS;WITH_RELAY;WITH_DBG;NO_DBG" "VERSION" "" ${ARGN})
    get_filename_component(file ${dllname} NAME_WLE)
    if(NOT spec_file MATCHES ".*\\.spec")
        message(FATAL_ERROR "spec2def only takes spec files as input.")
    endif()
    if(spec_WITH_RELAY)
        set(with_relay_arg "--with-tracing")
    endif()
    if(spec_VERSION)
        set(version_arg "--version=0x${spec_VERSION}")
    else()
        set(version_arg "--version=${DLL_EXPORT_VERSION}")
    endif()
    if(spec_WITH_DBG OR (DBG AND NOT spec_NO_DBG))
        set(dbg_arg "--dbg")
    endif()
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${file}.def ${CMAKE_CURRENT_BINARY_DIR}/${file}_stubs.c
        COMMAND native-spec2def -n=${dllname} -a=${ARCH2}
            -d=${CMAKE_CURRENT_BINARY_DIR}/${file}.def
            -s=${CMAKE_CURRENT_BINARY_DIR}/${file}_stubs.c
            ${with_relay_arg} ${version_arg} ${dbg_arg} ${CMAKE_CURRENT_SOURCE_DIR}/${spec_file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${spec_file} native-spec2def)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${file}_stubs.c
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    if(spec_ADD_IMPORTLIB)
        set(extra_flags)
        if(spec_NO_PRIVATE_WARNINGS)
            set(extra_flags --no-private-warnings)
        endif()
        generate_import_lib(lib${file} ${dllname} ${spec_file} ${extra_flags} "${version_arg}" "${dbg_arg}")
    endif()
endfunction()

macro(macro_mc flag file)
    set(COMMAND_MC ${CMAKE_MC_COMPILER} -u ${flag} -b -h ${CMAKE_CURRENT_BINARY_DIR}/ -r ${CMAKE_CURRENT_BINARY_DIR}/ ${file})
endmacro()

function(allow_warnings module)
endfunction()

include(sdk/cmake/riscv64-native.cmake)
