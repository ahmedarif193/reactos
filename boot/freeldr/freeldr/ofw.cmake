##
## PROJECT:     FreeLoader
## LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
## PURPOSE:     Build definitions for Open Firmware (PowerPC little-endian)
##

include_directories(BEFORE
    ${REACTOS_SOURCE_DIR}/boot/freeldr/freeldr
    ${REACTOS_SOURCE_DIR}/boot/freeldr/freeldr/include
    ${CMAKE_CURRENT_BINARY_DIR})

list(APPEND OFWLDR_ARC_SOURCE
    ${FREELDR_ARC_SOURCE}
    arch/powerpc/ofw.c
    arch/powerpc/ofwcon.c
    arch/powerpc/ofwdisk.c
    arch/powerpc/ofwhw.c
    arch/powerpc/ofwmem.c
    arch/powerpc/ofwsetup.c
    arch/powerpc/ofwvid.c)

list(APPEND OFWLDR_COMMON_ASM_SOURCE
    arch/powerpc/ofwgate.S
    ntldr/arch/ppc/ppcjump.S)

list(APPEND OFWLDR_BOOTMGR_SOURCE
    ${FREELDR_BOOTMGR_SOURCE}
    settingsmenu.c
    oslist.c)

add_asm_files(ofwfreeldr_common_asm ${FREELDR_COMMON_ASM_SOURCE} ${OFWLDR_COMMON_ASM_SOURCE})

list(APPEND FREELDR_NTLDR_SOURCE
    ${REACTOS_SOURCE_DIR}/ntoskrnl/config/cmboot.c
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ke/config.c
    ntldr/startup.c
    ntldr/conversion.c
    ntldr/headless.c
    ntldr/inffile.c
    ntldr/ntldropts.c
    ntldr/registry.c
    ntldr/setupldr.c
    ntldr/winldr.c
    ntldr/wlmemory.c
    ntldr/wlregistry.c
    ntldr/arch/ppc/winldr.c)

add_library(ofwfreeldr_common
    ${ofwfreeldr_common_asm}
    ${OFWLDR_ARC_SOURCE}
    ${FREELDR_BOOTLIB_SOURCE}
    ${OFWLDR_BOOTMGR_SOURCE}
    ${FREELDR_NTLDR_SOURCE})

target_link_libraries(ofwfreeldr_common setjmp fatfs)
target_compile_definitions(ofwfreeldr_common PRIVATE _FRLDRLIB_ OFWBOOT)
if(FREELDR_WIM_RAMDISK)
    target_compile_definitions(ofwfreeldr_common PRIVATE FREELDR_WIM_RAMDISK=1)
endif()
add_dependencies(ofwfreeldr_common bugcodes asm xdk)

list(APPEND OFWLDR_BASE_SOURCE
    arch/powerpc/ofwldr.c
    bootmgr.c
    ${FREELDR_BASE_SOURCE})

add_executable(ofwldr ${OFWLDR_BASE_SOURCE})
target_compile_definitions(ofwldr PRIVATE _FRLDRLIB_ OFWBOOT)

# Stage0 copies the image to its preferred base below itself (0x01000000);
# it runs there 1:1 under Open Firmware.
set_image_base(ofwldr 0x00100000)
target_link_options(ofwldr PRIVATE -Wl,--exclude-all-symbols,--file-alignment,0x200,--section-alignment,0x1000)
set_subsystem(ofwldr native)
set_entrypoint(ofwldr OfwEntry)

target_link_libraries(ofwldr ofwfreeldr_common cportlib blcmlib blrtl libcntpr)
if(FREELDR_WIM_RAMDISK)
    target_link_libraries(ofwldr freeldr_wimcore)
endif()
target_link_libraries(ofwldr setjmp)
add_dependencies(ofwldr xdk)

# Keep an unstripped copy for debugging; stage0 embeds the stripped image,
# since it copies every section, debug sections included, into memory.
add_custom_command(TARGET ofwldr
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ofwldr> ${CMAKE_CURRENT_BINARY_DIR}/ofwldr.dbg.exe
    COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:ofwldr>)

# Big-endian stage0 wrapping the loader: the image Open Firmware starts.
get_filename_component(_ppc_llvm_bin ${CMAKE_C_COMPILER} DIRECTORY)
set(_stage0_dir ${CMAKE_CURRENT_SOURCE_DIR}/arch/powerpc/stage0)
set(_stage0_flags --target=powerpc-unknown-elf -mcpu=604 -O2 -ffreestanding -fno-builtin -fno-pic -msoft-float -fno-stack-protector)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ppcboot.bin ${CMAKE_CURRENT_BINARY_DIR}/ppcboot.elf
    COMMAND ${CMAKE_C_COMPILER} ${_stage0_flags} -c ${_stage0_dir}/stage0.c -o ${CMAKE_CURRENT_BINARY_DIR}/stage0_c.o
    COMMAND ${CMAKE_C_COMPILER} ${_stage0_flags} -DSTAGE0_PAYLOAD=\"$<TARGET_FILE:ofwldr>\" -c ${_stage0_dir}/stage0.S -o ${CMAKE_CURRENT_BINARY_DIR}/stage0_s.o
    COMMAND ${_ppc_llvm_bin}/ld.lld -m elf32ppc -N -T ${_stage0_dir}/stage0.ld ${CMAKE_CURRENT_BINARY_DIR}/stage0_s.o ${CMAKE_CURRENT_BINARY_DIR}/stage0_c.o -o ${CMAKE_CURRENT_BINARY_DIR}/ppcboot.elf
    COMMAND ${_ppc_llvm_bin}/llvm-objcopy -O binary ${CMAKE_CURRENT_BINARY_DIR}/ppcboot.elf ${CMAKE_CURRENT_BINARY_DIR}/ppcboot.bin
    DEPENDS ofwldr ${_stage0_dir}/stage0.c ${_stage0_dir}/stage0.S ${_stage0_dir}/stage0.ld
    VERBATIM)
add_custom_target(ppcboot ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ppcboot.bin)
