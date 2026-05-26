list(APPEND SOURCE
    k32_process_hacks.c
    srw_try_hacks.c)

add_library(llvmcompat STATIC ${SOURCE})
add_dependencies(llvmcompat psdk)
target_link_libraries(llvmcompat INTERFACE libkernel32_vista libpsapi)
