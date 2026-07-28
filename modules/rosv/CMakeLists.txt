add_asm_files(rosv_asm
    vmx/vmx_asm.S)

set(ROSV_SOURCES
    rosv.c
    device/console.c
    device/fs9p.c
    device/io.c
    device/net_backend.c
    device/pty.c
    device/rtl8139.c
    device/uart.c
    device/vhdx_alloc.c
    device/vhdx_bat.c
    device/vhdx_crc32c.c
    device/vhdx_diff.c
    device/vhdx_log.c
    device/vhdx_parser.c
    device/virtio_blk.c
    device/virtio_console.c
    device/virtio_net.c
    ept/ept.c
    ept/ept_exit.c
    loader/bootparams.c
    loader/bzimage.c
    loader/entry.c
    loader/image.c
    loader/initrd.c
    vm/memory.c
    vm/session.c
    vm/vcpu.c
    vm/vm.c
    vm/vm_debug.c
    vm/vm_log.c
    vmx/exit.c
    vmx/insn_decode.c
    vmx/vmcs.c
    vmx/vmx.c
    vmx/vmx_debug.c
    ${rosv_asm})

add_library(rosv MODULE ${ROSV_SOURCES})
target_include_directories(rosv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_module_type(rosv kernelmodedriver)
add_importlibs(rosv netio ntoskrnl hal)
add_cd_file(TARGET rosv DESTINATION reactos/system32/drivers FOR all)

add_subdirectory(tools)
