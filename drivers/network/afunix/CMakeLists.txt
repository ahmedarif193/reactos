
include_directories(
    BEFORE .
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

list(APPEND SOURCE
    afunix.c
    afunix.h)

add_library(afunix MODULE ${SOURCE} afunix.rc)
set_module_type(afunix kernelmodedriver)
add_importlibs(afunix ntoskrnl hal)
add_pch(afunix afunix.h SOURCE)
add_cd_file(TARGET afunix DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(afunix_reg.inf)
