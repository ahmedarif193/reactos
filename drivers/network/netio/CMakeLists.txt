
spec2def(netio.sys netio.spec ADD_IMPORTLIB)

# Including AFD is needed by the TDI helpers.
# They have been part of AFD before.

include_directories(
    BEFORE include
    ../afd/include
    ../tdihelpers/include
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

list(APPEND SOURCE
    netio.c
    nmr_stubs.c
    fwp_stubs.c
    ../tdihelpers/tdi.c
    ../tdihelpers/tdiconn.c
    ${CMAKE_CURRENT_BINARY_DIR}/netio.def)

add_library(netio MODULE ${SOURCE})
target_link_libraries(netio ${PSEH_LIB})
# NETIO.SYS builds for the NT 6.1 target. Override the global NT target
# explicitly without triggering command-line macro redefinition warnings.
target_compile_options(netio PRIVATE
    -UNTDDI_VERSION -DNTDDI_VERSION=0x06010000
    -U_WIN32_WINNT -D_WIN32_WINNT=0x0601
    -UWINVER -DWINVER=0x0601)
set_module_type(netio kernelmodedriver)
add_importlibs(netio ntoskrnl hal)
add_cd_file(TARGET netio DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(netio_reg.inf)
