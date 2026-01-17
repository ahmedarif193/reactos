
include(ExternalProject)

function(_ros_collect_host_tool_sources _tool _out_var)
    set(_sources)

    set(_tool_base "${REACTOS_SOURCE_DIR}/sdk/tools/${_tool}")
    if(IS_DIRECTORY "${_tool_base}")
        file(GLOB_RECURSE _dir_sources CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
            "${_tool_base}/*.c"
            "${_tool_base}/*.cc"
            "${_tool_base}/*.cpp"
            "${_tool_base}/*.cxx"
            "${_tool_base}/*.h"
            "${_tool_base}/*.hh"
            "${_tool_base}/*.hpp"
            "${_tool_base}/*.hxx"
            "${_tool_base}/*.inl"
            "${_tool_base}/*.idl"
            "${_tool_base}/*.l"
            "${_tool_base}/*.y"
            "${_tool_base}/*.lex"
            "${_tool_base}/*.yacc"
            "${_tool_base}/*.asm"
            "${_tool_base}/*.s"
            "${_tool_base}/*.rc")
        list(APPEND _sources ${_dir_sources})
    endif()

    foreach(_ext IN LISTS CMAKE_C_SOURCE_FILE_EXTENSIONS CMAKE_CXX_SOURCE_FILE_EXTENSIONS)
        if(EXISTS "${_tool_base}.${_ext}")
            list(APPEND _sources "${_tool_base}.${_ext}")
        endif()
    endforeach()

    foreach(_ext h hh hpp hxx inl idl l y lex yacc asm s rc)
        if(EXISTS "${_tool_base}.${_ext}")
            list(APPEND _sources "${_tool_base}.${_ext}")
        endif()
    endforeach()

    list(REMOVE_DUPLICATES _sources)
    set(${_out_var} "${_sources}" PARENT_SCOPE)
endfunction()

function(setup_host_tools)
    if(DEFINED REACTOS_TOP_BINARY_DIR AND NOT CMAKE_BINARY_DIR STREQUAL REACTOS_TOP_BINARY_DIR AND NOT DEFINED HOST_TOOLS_DIR)
        set(_top_host_tools "${REACTOS_TOP_BINARY_DIR}/host-tools/bin")
        if(EXISTS "${_top_host_tools}")
            set(HOST_TOOLS_DIR "${_top_host_tools}")
        endif()
    endif()

    list(APPEND HOST_TOOLS
        asmpp
        bin2c
        widl
        gendib
        cabman
        fatten
        hpp
        isohybrid
        mkhive
        mkisofs
        obj2bin
        spec2def
        geninc
        mkshelllink
        txt2nls
        utf16le
        xml2sdb
        gen_syscalls)
    if(NOT MSVC)
        list(APPEND HOST_TOOLS pefixup)
        # rsym tool removed - DWARF sections are used directly for symbol resolution
    endif()

    foreach(_tool ${HOST_TOOLS})
        _ros_collect_host_tool_sources(${_tool} _computed_sources)
        set(_var_name "HOST_TOOL_SOURCE_FILES_${_tool}")
        set(${_var_name} "${_computed_sources}")
    endforeach()
    if ((ARCH STREQUAL "amd64") AND (CMAKE_C_COMPILER_ID STREQUAL "GNU") AND (NOT DISABLE_GCC_PLUGIN_SEH))
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} --print-file-name=plugin
            OUTPUT_VARIABLE GCC_PLUGIN_DIR)
        string(STRIP ${GCC_PLUGIN_DIR} GCC_PLUGIN_DIR)
        list(APPEND CMAKE_HOST_TOOLS_EXTRA_ARGS -DGCC_PLUGIN_DIR=${GCC_PLUGIN_DIR})
        list(APPEND HOST_MODULES gcc_plugin_seh)
        if (CMAKE_HOST_WIN32)
            list(APPEND HOST_MODULES g++_plugin_seh)
        endif()
    endif()
    if (CMAKE_HOST_WIN32)
        if(MSVC_IDE)
            set(HOST_EXTRA_DIR "$(ConfigurationName)/")
        endif()
        set(HOST_EXE_SUFFIX ".exe")
        set(HOST_MODULE_SUFFIX ".dll")
    else()
        set(HOST_MODULE_SUFFIX ".so")
    endif()

    set(_reused_host_tools FALSE)
    if(DEFINED HOST_CC)
        list(APPEND CMAKE_HOST_TOOLS_EXTRA_ARGS -DCMAKE_C_COMPILER=${HOST_CC})
    endif()
    if(DEFINED HOST_CXX)
        list(APPEND CMAKE_HOST_TOOLS_EXTRA_ARGS -DCMAKE_CXX_COMPILER=${HOST_CXX})
    endif()
    if(APPLE)
        set(_host_bison "/opt/homebrew/opt/bison/bin/bison")
        if(EXISTS "${_host_bison}")
            list(APPEND CMAKE_HOST_TOOLS_EXTRA_ARGS -DBISON_EXECUTABLE=${_host_bison})
        endif()
        set(_host_flex "/opt/homebrew/opt/flex/bin/flex")
        if(EXISTS "${_host_flex}")
            list(APPEND CMAKE_HOST_TOOLS_EXTRA_ARGS -DFLEX_EXECUTABLE=${_host_flex})
        endif()
    endif()
    if(DEFINED HOST_TOOLS_DIR)
        file(REAL_PATH "${HOST_TOOLS_DIR}" _host_tools_bin)
        if(NOT EXISTS "${_host_tools_bin}")
            message(FATAL_ERROR "Specified HOST_TOOLS_DIR '${HOST_TOOLS_DIR}' does not exist")
        endif()
        set(_reused_host_tools TRUE)
        add_custom_target(host-tools)
        get_filename_component(INSTALL_DIR "${_host_tools_bin}" DIRECTORY)
    else()
        list(TRANSFORM HOST_TOOLS PREPEND "${REACTOS_BINARY_DIR}/host-tools/bin/" OUTPUT_VARIABLE HOST_TOOLS_OUTPUT)
        if (CMAKE_HOST_WIN32)
            list(TRANSFORM HOST_TOOLS_OUTPUT APPEND ".exe")
        endif()
    endif()

    # Normalize to the same format as our own ARCH, and add one for the VC shell
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" lowercase_CMAKE_HOST_SYSTEM_PROCESSOR)
    if(lowercase_CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL x86 OR lowercase_CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^i[3456]86$")
        set(HOST_ARCH i386)
        set(VCVARSALL_ARCH x86)
    elseif(lowercase_CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL x86_64 OR lowercase_CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL amd64)
        set(HOST_ARCH amd64)
        set(VCVARSALL_ARCH amd64_x86) # x64 host-tools are not happy compiling for x86...
    elseif(lowercase_CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL arm)
        set(HOST_ARCH arm)
        set(VCVARSALL_ARCH arm)
    # 'aarch64' is used in GNU tools instead of 'arm64'
    elseif(lowercase_CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL arm64 OR lowercase_CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL aarch64)
        set(HOST_ARCH arm64)
        set(VCVARSALL_ARCH arm64)
    else()
        message(FATAL_ERROR "Unknown host architecture: ${lowercase_CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()

    if(ARCH STREQUAL HOST_ARCH)
        set(HOST_TOOLS_CMAKE_COMMAND "${CMAKE_COMMAND}")
        message("Not cross-compiling, no special host-tools cmake command")
    elseif(MSVC)
        message("Compiling on ${HOST_ARCH} for ${ARCH} (MSVC)")
        set(HOST_TOOLS_CMAKE_COMMAND "${REACTOS_BINARY_DIR}/host-tools/cmake_shim.cmd")
        if(MSVC_VERSION EQUAL 1900)
            file(WRITE ${HOST_TOOLS_CMAKE_COMMAND}
                "set VSCMD_SKIP_SENDTELEMETRY=1\n"
                "@call \"$ENV{VCINSTALLDIR}\\vcvarsall.bat\" ${VCVARSALL_ARCH}\n"
                "\"${CMAKE_COMMAND}\" %*"
            )
        elseif(MSVC_VERSION GREATER_EQUAL 1910)
            # 2017 and 2019 use the same folder structure
            file(WRITE ${HOST_TOOLS_CMAKE_COMMAND}
                "@set VSCMD_ARG_no_logo=1\n"
                "@call \"$ENV{VCINSTALLDIR}\\Auxiliary\\Build\\vcvarsall.bat\" /clean_env\n"
                "@call \"$ENV{VCINSTALLDIR}\\Auxiliary\\Build\\vcvarsall.bat\" ${VCVARSALL_ARCH}\n"
                "\"${CMAKE_COMMAND}\" %*"
            )
        else()
            message(FATAL "Unable to figure out vcvarsall path")
        endif()
    else()
        set(HOST_TOOLS_CMAKE_COMMAND "${CMAKE_COMMAND}")
        message("Cross-compiling on non-msvc, no special host-tools cmake command")
    endif()

    # CMake might choose clang if it finds it in the PATH. Always prefer cl for host tools
    if (MSVC)
        list(APPEND CMAKE_HOST_TOOLS_EXTRA_ARGS
            -DCMAKE_C_COMPILER=cl
            -DCMAKE_CXX_COMPILER=cl)
    endif()

    if (MSVC_IDE)
        # Required for Bison/Flex wrappers created by /CMakeLists.txt.
        list(APPEND CMAKE_HOST_TOOLS_EXTRA_ARGS
            -DROS_SAVED_BISON_PKGDATADIR=${ROS_SAVED_BISON_PKGDATADIR}
            -DROS_SAVED_M4=${ROS_SAVED_M4}
            )
    endif()

    if(NOT DEFINED HOST_BUILD_TYPE)
        set(HOST_BUILD_TYPE Debug)
    endif()

    if(NOT _reused_host_tools)
        ExternalProject_Add(host-tools
            SOURCE_DIR ${REACTOS_SOURCE_DIR}
            PREFIX ${REACTOS_BINARY_DIR}/host-tools
            BINARY_DIR ${REACTOS_BINARY_DIR}/host-tools/bin
            CMAKE_COMMAND ${HOST_TOOLS_CMAKE_COMMAND}
            CMAKE_ARGS
                -UCMAKE_TOOLCHAIN_FILE
                -DCMAKE_RULE_MESSAGES:BOOL=OFF
                -DARCH:STRING=${ARCH}
                -DWOW64_MULTILIB=OFF
                -DCMAKE_INSTALL_PREFIX=${REACTOS_BINARY_DIR}/host-tools
                -DTOOLS_FOLDER=${REACTOS_BINARY_DIR}/host-tools/bin
                -DTARGET_COMPILER_ID=${CMAKE_C_COMPILER_ID}
                -DTARGET_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DCMAKE_BUILD_TYPE=${HOST_BUILD_TYPE}
                ${CMAKE_HOST_TOOLS_EXTRA_ARGS}
            BUILD_ALWAYS TRUE
            INSTALL_COMMAND ${CMAKE_COMMAND} -E true
            BUILD_BYPRODUCTS ${HOST_TOOLS_OUTPUT}
        )

        ExternalProject_Get_Property(host-tools INSTALL_DIR)
    endif()

    if(TARGET wow64_multilib_i386)
        add_dependencies(wow64_multilib_i386 host-tools)
    endif()
    if(TARGET wow64_multilib_stage)
        add_dependencies(wow64_multilib_stage host-tools)
    endif()

    foreach(_tool ${HOST_TOOLS})
        add_executable(native-${_tool} IMPORTED)
        set_target_properties(native-${_tool} PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/bin/${HOST_EXTRA_DIR}${_tool}${HOST_EXE_SUFFIX})
        add_dependencies(native-${_tool} host-tools ${INSTALL_DIR}/bin/${HOST_EXTRA_DIR}${_tool}${HOST_EXE_SUFFIX})

        set(_sources_var "HOST_TOOL_SOURCE_FILES_${_tool}")
        if(DEFINED ${_sources_var} AND NOT "${${_sources_var}}" STREQUAL "")
            set_property(TARGET native-${_tool} PROPERTY ROS_HOST_TOOL_SOURCES "${${_sources_var}}")
        endif()
    endforeach()

    foreach(_module ${HOST_MODULES})
        add_library(native-${_module} MODULE IMPORTED)
        set_target_properties(native-${_module} PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/bin/${HOST_EXTRA_DIR}${_module}${HOST_MODULE_SUFFIX})
        add_dependencies(native-${_module} host-tools ${INSTALL_DIR}/bin/${HOST_EXTRA_DIR}${_module}${HOST_MODULE_SUFFIX})
    endforeach()
endfunction()
