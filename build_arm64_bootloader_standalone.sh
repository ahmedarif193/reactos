#!/bin/bash

# Standalone ARM64 UEFI Bootloader Build
# This builds just the bootloader without full ReactOS dependencies

set -e

echo "ARM64 UEFI Bootloader Standalone Build"
echo "======================================="

# Create standalone build directory
BUILD_DIR="arm64-bootloader-standalone"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

# Create minimal CMakeLists.txt
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.17)
project(ARM64Bootloader C ASM)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Use clang for ARM64
set(CMAKE_C_COMPILER clang)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_C_FLAGS "-target aarch64-w64-mingw32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector")
set(CMAKE_ASM_FLAGS "-target aarch64-w64-mingw32")
set(CMAKE_EXE_LINKER_FLAGS "-nostdlib -Wl,--subsystem,10 -Wl,--entry,EfiEntry")

# Source files
set(ASM_SOURCES
    ../boot/freeldr/freeldr/arch/arm64/misc.S
    ../boot/freeldr/freeldr/arch/arm64/except.S
    ../boot/freeldr/freeldr/arch/arm64/cache_v2.S
    ../boot/freeldr/freeldr/arch/uefi/arm64/uefiasm.S
)

# Create a simple UEFI entry point
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/uefi_entry.c "
typedef unsigned long long UINT64;
typedef unsigned int UINT32;
typedef unsigned short UINT16;
typedef unsigned char UINT8;
typedef void* EFI_HANDLE;
typedef void* EFI_SYSTEM_TABLE;

__attribute__((ms_abi))
UINT64 EfiEntry(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    // Simple infinite loop for testing
    while(1) {
        __asm__ volatile(\"wfe\");
    }
    return 0;
}
")

# Build bootloader
add_executable(bootaa64.efi
    ${ASM_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/uefi_entry.c
)

# Set output properties
set_target_properties(bootaa64.efi PROPERTIES
    SUFFIX ".efi"
    LINK_FLAGS "-Wl,--subsystem,10"
)
EOF

echo "Configuring..."
cmake -G Ninja .

echo "Building..."
ninja -v

echo ""
echo "Build complete!"
echo "Output: $PWD/bootaa64.efi"

# Create disk image
echo ""
echo "Creating disk image..."
dd if=/dev/zero of=arm64boot.img bs=1M count=32 2>/dev/null
mkfs.vfat -F 32 arm64boot.img
mmd -i arm64boot.img ::/EFI
mmd -i arm64boot.img ::/EFI/BOOT
mcopy -i arm64boot.img bootaa64.efi ::/EFI/BOOT/BOOTAA64.EFI

echo "Disk image created: $PWD/arm64boot.img"
echo ""
echo "To test with QEMU:"
echo "  qemu-system-aarch64 -M virt -cpu cortex-a72 -m 2048 \\"
echo "    -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \\"
echo "    -drive file=$PWD/arm64boot.img,format=raw,if=virtio \\"
echo "    -nographic -serial mon:stdio"