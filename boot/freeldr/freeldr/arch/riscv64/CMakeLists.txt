# The RISC-V port currently implements the firmware boot-manager stage.
# NT kernel handoff is deliberately not advertised by this target.
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "RISC-V FreeLdr requires Clang")
endif()
set(EDK2_SOURCE_DIR "" CACHE PATH "EDK2 checkout providing GenFw and UEFI headers")
find_program(GENFW GenFw HINTS "${EDK2_SOURCE_DIR}/BaseTools/Source/C/bin" REQUIRED)
if(NOT EXISTS "${EDK2_SOURCE_DIR}/MdePkg/Include/RiscV64/ProcessorBind.h")
    message(FATAL_ERROR "Set EDK2_SOURCE_DIR to an EDK2 checkout with RISC-V support")
endif()
find_program(HOST_CC NAMES cc gcc REQUIRED)
set(ROS "${REACTOS_SOURCE_DIR}")
set(FRLDR "${ROS}/boot/freeldr/freeldr")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${GENERATED}")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hpp"
    COMMAND ${HOST_CC} -O2 "${ROS}/sdk/tools/hpp/hpp.c" -o "${CMAKE_CURRENT_BINARY_DIR}/hpp"
    DEPENDS "${ROS}/sdk/tools/hpp/hpp.c" VERBATIM)
file(GLOB_RECURSE XDK_HEADERS "${ROS}/sdk/include/xdk/*.h")
add_custom_command(OUTPUT "${GENERATED}/ntdef.h"
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/hpp" ntdef.template.h "${GENERATED}/ntdef.h"
    WORKING_DIRECTORY "${ROS}/sdk/include/xdk"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/hpp" ${XDK_HEADERS} VERBATIM)

set(BOOTMGR_SOURCES bootmgr.c settings.c oslist.c custom.c options.c arcname.c
    ntldr/advopts.c ntldr/ntldropts.c lib/arcsupp.c
    lib/inifile/ini_init.c lib/inifile/inifile.c lib/inifile/parse.c
    ui/directui.c ui/minitui.c ui/noui.c ui/tui.c ui/tuimenu.c ui/ui.c ui/video.c)
list(TRANSFORM BOOTMGR_SOURCES PREPEND "${FRLDR}/")
add_library(riscv_bootmgr OBJECT ${BOOTMGR_SOURCES} "${GENERATED}/ntdef.h")
target_include_directories(riscv_bootmgr PUBLIC "${GENERATED}" "${FRLDR}/include" "${FRLDR}"
    "${ROS}/sdk/include" "${ROS}/sdk/include/psdk" "${ROS}/sdk/include/ddk"
    "${ROS}/sdk/include/vcruntime" "${ROS}/sdk/include/crt" "${ROS}/sdk/include/reactos"
    "${ROS}/sdk/lib/pseh/include")
target_compile_definitions(riscv_bootmgr PUBLIC _WIN32 _WIN64 __ROS_LONG64__
    _M_RISCV64 _RISCV64_ _NTSYSTEM_ _FRLDRLIB_ UEFIBOOT USE_DUMMY_PSEH FREELDR_UEFI_TEXT_ONLY
    NTDDI_VERSION=0x05020100 _WIN32_WINNT=0x502 COPYRIGHT_YEAR="2026")
target_compile_options(riscv_bootmgr PUBLIC -ffreestanding -fno-builtin -fshort-wchar
    -fms-extensions -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
    -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter
    -Wno-ignored-attributes -Wno-multichar -Wno-pragma-pack)

set(CRT_SOURCES mem/memcpy.c mem/memmove.c mem/memset.c mem/memcmp.c
    string/strlen.c string/strcmp.c string/strncmp.c string/strcpy.c string/strncpy.c
    string/strchr.c string/strrchr.c string/strstr.c string/strcat.c string/strncat.c
    string/_stricmp_nt.c string/_strnicmp_nt.c string/_strlwr_nt.c string/_strupr_nt.c
    string/ctype.c string/atoi.c string/atoi64.c string/strtoull.c
    string/strcspn.c string/strnlen.c string/wcsnlen.c
    string/wcslen.c printf/_vsnprintf.c printf/_snprintf.c printf/vsprintf.c
    printf/sprintf.c printf/streamout.c)
list(TRANSFORM CRT_SOURCES PREPEND "${ROS}/sdk/lib/crt/")
add_library(riscv_bootcrt STATIC ${CRT_SOURCES})
target_link_libraries(riscv_bootcrt PRIVATE riscv_bootmgr)
target_include_directories(riscv_bootcrt PRIVATE "${ROS}/sdk/lib/crt/include")
target_compile_definitions(riscv_bootcrt PRIVATE _LIBCNT_ _CRT_NOFLOAT)

add_executable(uefildr firmware.c runtime.c)
target_link_libraries(uefildr PRIVATE riscv_bootmgr)
target_link_libraries(uefildr PRIVATE riscv_bootcrt)
target_include_directories(uefildr PRIVATE "${EDK2_SOURCE_DIR}/MdePkg/Include"
    "${EDK2_SOURCE_DIR}/MdePkg/Include/RiscV64")
set_target_properties(uefildr PROPERTIES SUFFIX ".elf")
target_link_options(uefildr PRIVATE -nostdlib -fuse-ld=lld
    -Wl,--entry=EfiEntry,--emit-relocs,--gc-sections,--no-relax
    -Wl,-z,common-page-size=0x1000
    "-Wl,-T,${EDK2_SOURCE_DIR}/BaseTools/Scripts/GccBase.lds"
    -Wl,--defsym=PECOFF_HEADER_SIZE=0x240 "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/uefildr.map")
set_property(TARGET uefildr APPEND PROPERTY LINK_DEPENDS "${EDK2_SOURCE_DIR}/BaseTools/Scripts/GccBase.lds")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/uefildr.efi"
    COMMAND ${GENFW} -e UEFI_APPLICATION -o "${CMAKE_CURRENT_BINARY_DIR}/uefildr.efi" $<TARGET_FILE:uefildr>
    DEPENDS uefildr VERBATIM)
add_custom_target(freeldr_efi ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/uefildr.efi")
