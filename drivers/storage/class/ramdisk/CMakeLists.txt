
include_directories(..)
# SetupLdrBlock references are now guarded with NTDDI_VERSION < NTDDI_WIN7.
add_definitions(-DNTDDI_VERSION=${REACTOS_TARGET_NTDDI})
add_library(ramdisk MODULE ramdisk.c ramdisk.rc)
set_module_type(ramdisk kernelmodedriver)
add_importlibs(ramdisk ntoskrnl hal)
add_cd_file(TARGET ramdisk DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(ramdisk ramdisk.inf)
