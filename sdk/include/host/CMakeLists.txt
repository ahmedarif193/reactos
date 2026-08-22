
add_library(host_includes INTERFACE)
target_include_directories(host_includes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(host_includes INTERFACE -Wno-pragma-pack)
endif()
