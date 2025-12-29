
list(APPEND LIBCNTPR_SOURCE
    ${LIBCNTPR_EXCEPT_SOURCE}
    ${LIBCNTPR_FLOAT_SOURCE}
    ${LIBCNTPR_MATH_SOURCE}
    ${LIBCNTPR_MBSTRING_SOURCE}
    ${LIBCNTPR_MEM_SOURCE}
    ${LIBCNTPT_MISC_SOURCE}
    ${LIBCNTPR_PRINTF_SOURCE}
    ${LIBCNTPR_SEARCH_SOURCE}
    ${LIBCNTPR_STDIO_SOURCE}
    ${LIBCNTPR_STDLIB_SOURCE}
    ${LIBCNTPR_STRING_SOURCE}
    ${LIBCNTPR_WSTRING_SOURCE}
)

if(ARCH STREQUAL "arm64")
    list(APPEND LIBCNTPR_SOURCE
        except/arm64/fastfail.c
    )
endif()

list(APPEND LIBCNTPR_ASM_SOURCE
    ${LIBCNTPR_EXCEPT_ASM_SOURCE}
    ${LIBCNTPR_FLOAT_ASM_SOURCE}
    ${LIBCNTPR_MATH_ASM_SOURCE}
    ${LIBCNTPR_MEM_ASM_SOURCE}
    ${LIBCNTPR_STRING_ASM_SOURCE}
)

set_source_files_properties(${LIBCNTPR_ASM_SOURCE} PROPERTIES COMPILE_DEFINITIONS "NO_RTL_INLINES;_NTSYSTEM_;_NTDLLBUILD_;_LIBCNT_;__CRT__NO_INLINE;CRTDLL")
add_asm_files(libcntpr_asm ${LIBCNTPR_ASM_SOURCE})

add_library(libcntpr STATIC ${LIBCNTPR_SOURCE} ${libcntpr_asm})
target_compile_definitions(libcntpr
 PRIVATE    NO_RTL_INLINES
    _NTSYSTEM_
    _NTDLLBUILD_
    _LIBCNT_
    __CRT__NO_INLINE
    CRTDLL)
if(ARCH STREQUAL "arm64" AND TARGET msvcrt_host)
    target_link_libraries(libcntpr INTERFACE msvcrt_host)
endif()
if(ARCH STREQUAL "arm64" AND NOT TARGET pthread_stubs)
    add_library(pthread_stubs STATIC arm64/pthread_stubs.c)
endif()
if(ARCH STREQUAL "arm64")
    if(TARGET libatomic_host)
        target_link_libraries(libcntpr INTERFACE libatomic_host pthread_stubs)
    elseif(TARGET libatomic)
        target_link_libraries(libcntpr INTERFACE libatomic pthread_stubs)
    else()
        find_library(_LIBATOMIC_FALLBACK NAMES atomic)
        if(_LIBATOMIC_FALLBACK)
            target_link_libraries(libcntpr INTERFACE atomic pthread_stubs)
        else()
            target_link_libraries(libcntpr INTERFACE pthread_stubs)
        endif()
        unset(_LIBATOMIC_FALLBACK)
    endif()
    get_target_property(_libgcc_path libgcc IMPORTED_LOCATION)
    if(_libgcc_path)
        target_link_libraries(libcntpr INTERFACE ${_libgcc_path})
    endif()
elseif(TARGET libatomic_host)
    target_link_libraries(libcntpr INTERFACE libatomic_host)
elseif(TARGET libatomic)
    target_link_libraries(libcntpr INTERFACE libatomic)
elseif(NOT WIN32)
    target_link_libraries(libcntpr INTERFACE atomic)
endif()
add_dependencies(libcntpr psdk asm)
