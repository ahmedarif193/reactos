list(APPEND SOURCE
     close.c
     create.c
     ea.c
     fastio.c
     fileinfo.c
     fsctrl.c
     ntfsinit.c
     km/host.c
     read.c
     vol.c
     write.c
)

add_library(ntfs3g MODULE ${SOURCE} ntfs.rc)
set_module_type(ntfs3g kernelmodedriver)
target_include_directories(ntfs3g PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/ntfs3g/reactos/common)
target_link_libraries(ntfs3g memcmp ntfs3g_core libcntpr)
if(GDB AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ntfs3g PRIVATE -O0)
endif()
add_importlibs(ntfs3g ntoskrnl hal)
add_pch(ntfs3g ntfspch.h SOURCE)
add_cd_file(TARGET ntfs3g DESTINATION reactos/system32/drivers NO_CAB FOR all)
