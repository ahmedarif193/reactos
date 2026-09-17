# Reuse the kernel support libraries and their source lists with the NT ABI.
# Firmware libraries remain in their separate UEFI compilation scope.
set(CMAKE_INCLUDE_CURRENT_DIR ON)
link_libraries(riscv_nt)

# Native RISC-V assembly uses the compiler's GNU assembler syntax directly.
macro(add_asm_files _target)
    set(${_target} ${ARGN})
endmacro()

add_subdirectory(apisets)
add_subdirectory(cmlib)
add_subdirectory(cportlib)
add_subdirectory(crt)
add_subdirectory(cryptlib)
add_subdirectory(drivers/arbiter)
add_subdirectory(drivers/csq)
add_subdirectory(drivers/ntoskrnl_vista)
add_dependencies(ntoskrnl_vista psdk)
add_subdirectory(ioevent)
add_subdirectory(lsalib)
add_subdirectory(poguid)
add_subdirectory(pseh)
add_subdirectory(rtl)
add_subdirectory(wdmguid)
