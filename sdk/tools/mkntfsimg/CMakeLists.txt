add_host_tool(mkntfsimg
    mkntfsimg.c
    io.c
    volume.c
    mftio.c
    mftalloc.c
    mft_extend.c
    attr.c
    builder.c
    index_rewrite.c
    indx.c
    index_alloc.c
    cluster_alloc.c
    data_attr.c
    path.c
    lookup.c
    dir_insert.c
    spill.c
    index_alloc_extend.c
    addfiles.c
    mst.c
    bitmap.c
    runlist.c)

target_include_directories(mkntfsimg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT MSVC)
    target_compile_options(mkntfsimg PRIVATE -Wall -Wextra -Wno-unused-parameter)
else()
    target_compile_options(mkntfsimg PRIVATE /W3)
endif()
