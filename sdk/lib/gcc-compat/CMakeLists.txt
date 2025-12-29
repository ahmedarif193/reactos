
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/include)

list(APPEND SOURCE
    __mingw_vsprintf.c
    StringCchCopyNA.c)

if(ARCH STREQUAL "i386" AND REACTOS_MULTILIB_I386)
    list(APPEND SOURCE acrt_iob_multilib.c)
else()
    list(APPEND SOURCE atexit_wrap.c)
endif()

if ((CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang") AND WIN32)
    list(APPEND SOURCE rand_s.c)
endif()

add_library(stdc++compat ${SOURCE})
set_target_cpp_properties(stdc++compat WITH_EXCEPTIONS)
add_dependencies(stdc++compat xdk)
target_link_libraries(stdc++compat PRIVATE libmsvcrt cppstl)
remove_target_compile_option(stdc++compat "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<IN_LIST:cppstl,$<TARGET_PROPERTY:LINK_LIBRARIES>>>>:-nostdinc>")
target_compile_definitions(stdc++compat PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:PAL_STDCPP_COMPAT>" _CRT_DECLARE_NONSTDC_NAMES=1)
