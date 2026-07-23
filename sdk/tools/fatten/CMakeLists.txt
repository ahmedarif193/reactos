
set(FATFS_SOURCE_DIR ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/fatfs)

add_library(fatfs_host STATIC
    ${FATFS_SOURCE_DIR}/ff.c
    ${FATFS_SOURCE_DIR}/ffunicode.c)
target_include_directories(fatfs_host PUBLIC ${FATFS_SOURCE_DIR})

add_host_tool(fatten
    fatten.c
    diskio.c)
target_link_libraries(fatten PRIVATE fatfs_host host_includes)
if(NOT CMAKE_HOST_WIN32)
    target_compile_definitions(fatten PRIVATE _FILE_OFFSET_BITS=64 _POSIX_C_SOURCE=200809L)
endif()
