# Helper functions to consume the host MinGW-w64 CRT archives.

function(mingwcrt_locate_library _out_var)
    if(NOT ARGN)
        message(FATAL_ERROR "mingwcrt_locate_library requires at least one candidate name")
    endif()

    foreach(_candidate IN LISTS ARGN)
        string(TOUPPER "${_candidate}" _key)
        string(REGEX REPLACE "[^A-Z0-9]" "_" _key "${_key}")
        set(_cache_var "MINGWCRT_LIB_${_key}_PATH")
        if(DEFINED ${_cache_var} AND EXISTS "${${_cache_var}}")
            set(${_out_var} "${${_cache_var}}" PARENT_SCOPE)
            return()
        endif()

        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-file-name=lib${_candidate}.a
            OUTPUT_VARIABLE _path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_path AND NOT _path STREQUAL "lib${_candidate}.a")
            set(${_cache_var} "${_path}" CACHE FILEPATH "MinGW-w64 lib${_candidate}.a" FORCE)
            set(${_out_var} "${_path}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    message(FATAL_ERROR
        "USE_NATIVE_MINGW_CRT=ON but none of the libraries [${ARGN}] were found. "
        "Please install the MinGW-w64 CRT development files.")
endfunction()

function(mingwcrt_import_default_libs _out_var)
    set(options NO_MSVCRT)
    cmake_parse_arguments(MCRT "${options}" "" "" ${ARGN})

    if(MCRT_NO_MSVCRT)
        set(_cache_property MINGWCRT_IMPORTED_LIBRARIES_NOMSVCRT)
    else()
        set(_cache_property MINGWCRT_IMPORTED_LIBRARIES_DEFAULT)
    endif()

    get_property(_mingwcrt_libs_defined GLOBAL PROPERTY ${_cache_property} SET)
    if(_mingwcrt_libs_defined)
        get_property(_existing_libs GLOBAL PROPERTY ${_cache_property})
        set(${_out_var} "${_existing_libs}" PARENT_SCOPE)
        unset(_existing_libs)
        unset(_mingwcrt_libs_defined)
        unset(_cache_property)
        return()
    endif()

    set(_imports
        "mingw32"
        "gcc"
        "gcc_eh"
        "moldname"
        "mingwex"
        "msvcrt"
        "msvcrt-os"
        "ucrt|ucrtbase"
        "vcruntime|vcruntime140|vcruntime140_app"
        "supc++"
    )

    if(MCRT_NO_MSVCRT)
        list(REMOVE_ITEM _imports "msvcrt" "msvcrt-os")
    endif()

    set(_libs)
    foreach(_entry IN LISTS _imports)
        string(REPLACE "|" ";" _candidates "${_entry}")
        set(_location "")
        foreach(_candidate IN LISTS _candidates)
            if((_candidate STREQUAL "ucrt" OR _candidate STREQUAL "ucrtbase")
               AND EXISTS "${CMAKE_BINARY_DIR}/dll/win32/ucrtbase/libucrtbase.a")
                set(_location "${CMAKE_BINARY_DIR}/dll/win32/ucrtbase/libucrtbase.a")
                break()
            endif()
        endforeach()
        if(NOT _location)
            mingwcrt_locate_library(_location ${_candidates})
        endif()
        list(APPEND _libs "${_location}")
        unset(_candidates)
        unset(_location)
    endforeach()

    unset(_entry)
    unset(_imports)

    set(${_out_var} "${_libs}" PARENT_SCOPE)
    set_property(GLOBAL PROPERTY ${_cache_property} "${_libs}")
    unset(_libs)
    unset(_mingwcrt_libs_defined)
    unset(_cache_property)
    unset(MCRT_NO_MSVCRT)
endfunction()
