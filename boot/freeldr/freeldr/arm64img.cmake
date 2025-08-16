##
## PROJECT:     FreeLoader
## LICENSE:     GPL-2.0-or-later
## PURPOSE:     ARM64 bootloader-only image build
## COPYRIGHT:   Copyright 2025 ReactOS Team
##

# ARM64 Bootloader Image Target
# This creates a minimal image with just the bootloader for ARM64 testing

if(NOT ARCH STREQUAL "arm64")
    message(FATAL_ERROR "arm64img target is only available for ARM64 architecture")
endif()

# UEFI definitions are already included in main CMakeLists.txt
# Don't include again to avoid duplicate targets

# Create a custom target for ARM64 bootloader image
add_custom_target(arm64img
    DEPENDS uefildr
    COMMENT "Building ARM64 bootloader image"
)

# Create bootloader directory structure
add_custom_command(TARGET arm64img POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/arm64img/EFI/BOOT
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:uefildr> ${CMAKE_CURRENT_BINARY_DIR}/arm64img/EFI/BOOT/BOOTAA64.EFI
    COMMENT "Creating ARM64 UEFI boot structure"
)

# Create a simple startup.nsh for UEFI shell (optional, helps with testing)
add_custom_command(TARGET arm64img POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "\\EFI\\BOOT\\BOOTAA64.EFI" > ${CMAKE_CURRENT_BINARY_DIR}/arm64img/startup.nsh
    COMMENT "Creating startup.nsh"
)

# Create freeldr.ini configuration file for testing
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/freeldr_arm64.ini
"[FREELOADER]
DefaultOS=ReactOS
TimeOut=10

[ReactOS]
SystemPath=\\ReactOS
Options=/DEBUG /DEBUGPORT=SCREEN
")

add_custom_command(TARGET arm64img POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/freeldr_arm64.ini ${CMAKE_CURRENT_BINARY_DIR}/arm64img/freeldr.ini
    COMMENT "Creating freeldr.ini configuration"
)

# Create a disk image using dd (for QEMU testing)
add_custom_command(TARGET arm64img POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Creating ARM64 bootloader disk image..."
    COMMAND sh -c "dd if=/dev/zero of=${CMAKE_CURRENT_BINARY_DIR}/arm64boot.img bs=1M count=64 2>/dev/null || true"
    COMMAND mkfs.vfat -F 32 ${CMAKE_CURRENT_BINARY_DIR}/arm64boot.img || true
    COMMAND mmd -i ${CMAKE_CURRENT_BINARY_DIR}/arm64boot.img ::/EFI || true
    COMMAND mmd -i ${CMAKE_CURRENT_BINARY_DIR}/arm64boot.img ::/EFI/BOOT || true
    COMMAND mcopy -i ${CMAKE_CURRENT_BINARY_DIR}/arm64boot.img ${CMAKE_CURRENT_BINARY_DIR}/arm64img/EFI/BOOT/BOOTAA64.EFI ::/EFI/BOOT/ || true
    COMMAND mcopy -i ${CMAKE_CURRENT_BINARY_DIR}/arm64boot.img ${CMAKE_CURRENT_BINARY_DIR}/arm64img/freeldr.ini ::/ || true
    COMMAND mcopy -i ${CMAKE_CURRENT_BINARY_DIR}/arm64boot.img ${CMAKE_CURRENT_BINARY_DIR}/arm64img/startup.nsh ::/ || true
    COMMENT "Building FAT32 disk image for ARM64 bootloader"
    VERBATIM
)

# Create an ISO image (optional, for systems that support UEFI CD boot)
find_program(MKISOFS mkisofs genisoimage xorriso)
if(MKISOFS)
    add_custom_command(TARGET arm64img POST_BUILD
        COMMAND ${MKISOFS} -R -J -c boot.catalog -b arm64boot.img -no-emul-boot -boot-load-size 4 
                -o ${CMAKE_CURRENT_BINARY_DIR}/arm64boot.iso ${CMAKE_CURRENT_BINARY_DIR}/arm64img
        COMMENT "Creating ARM64 bootloader ISO image"
        VERBATIM
    )
endif()

# Add convenience target for testing with QEMU
add_custom_target(arm64img-qemu
    COMMAND qemu-system-aarch64 
            -M virt 
            -cpu cortex-a72 
            -m 2048 
            -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd 
            -drive file=${CMAKE_CURRENT_BINARY_DIR}/arm64boot.img,format=raw,if=virtio
            -nographic
            -serial mon:stdio
    DEPENDS arm64img
    COMMENT "Testing ARM64 bootloader with QEMU"
    VERBATIM
)

message(STATUS "ARM64 bootloader image target configured")
message(STATUS "Build with: ninja arm64img")
message(STATUS "Test with QEMU: ninja arm64img-qemu")