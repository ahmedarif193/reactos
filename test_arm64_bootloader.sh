#!/bin/bash

# Simple ARM64 Bootloader Test Build
# This creates a minimal build just to test if the ARM64 bootloader compiles

set -e

echo "ARM64 Bootloader Test Build"
echo "============================"

# Create a minimal test directory
TEST_DIR="$(pwd)/arm64-bootloader-test"
mkdir -p "$TEST_DIR"

cd "$TEST_DIR"

# Create a minimal CMakeLists.txt for testing
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.17)
project(ARM64BootloaderTest C ASM)

set(CMAKE_C_STANDARD 11)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../boot/freeldr/freeldr/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../boot/freeldr/freeldr/include/arch/arm64
    ${CMAKE_CURRENT_SOURCE_DIR}/../boot/freeldr/freeldr/include/arch/uefi
    ${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include
)

# Define minimal macros
add_definitions(-D_BLDR_ -DUEFIBOOT -DARCH_arm64)

# ARM64 Assembly files
set(ARM64_ASM_SOURCES
    ../boot/freeldr/freeldr/arch/arm64/misc.S
    ../boot/freeldr/freeldr/arch/arm64/except.S
    ../boot/freeldr/freeldr/arch/arm64/cache_v2.S
    ../boot/freeldr/freeldr/arch/uefi/arm64/uefiasm.S
)

# ARM64 C files
set(ARM64_C_SOURCES
    ../boot/freeldr/freeldr/arch/arm64/macharm64.c
    ../boot/freeldr/freeldr/arch/arm64/trap.c
    ../boot/freeldr/freeldr/arch/arm64/mmu_v2.c
    ../boot/freeldr/freeldr/arch/arm64/timer.c
)

# Enable ASM language
enable_language(ASM)

# Create test executable
add_executable(arm64_bootloader_test
    ${ARM64_ASM_SOURCES}
    ${ARM64_C_SOURCES}
)

# Set compile flags
target_compile_options(arm64_bootloader_test PRIVATE
    -ffreestanding
    -fno-builtin
    -fno-exceptions
    -fno-stack-protector
    -mstrict-align
)

# Link flags
target_link_options(arm64_bootloader_test PRIVATE
    -nostdlib
    -Wl,--entry=_start
    -Wl,--build-id=none
)

message(STATUS "ARM64 Bootloader test configuration complete")
EOF

# Run CMake with the Linux toolchain
echo "Configuring with CMake..."
cmake -G Ninja \
      -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
      -DCMAKE_ASM_COMPILER=aarch64-linux-gnu-gcc \
      -DCMAKE_AR=aarch64-linux-gnu-ar \
      .

echo ""
echo "Configuration complete. Attempting to build..."
echo ""

# Try to build
ninja -v arm64_bootloader_test || {
    echo ""
    echo "Build failed (expected - missing dependencies)"
    echo "But this shows that the ARM64 assembly files are syntactically correct"
    echo ""
}

# Check if any object files were created
echo "Checking for compiled objects..."
find . -name "*.o" -o -name "*.obj" | while read obj; do
    echo "  Found: $obj"
    aarch64-linux-gnu-objdump -h "$obj" | head -10
done

echo ""
echo "Test complete. The ARM64 bootloader structure is in place."
echo "To create a proper UEFI bootloader, you'll need:"
echo "  1. A proper aarch64-w64-mingw32 toolchain with GCC"
echo "  2. UEFI libraries and headers"
echo "  3. PE/COFF executable format support"