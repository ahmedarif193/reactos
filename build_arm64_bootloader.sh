#!/bin/bash

# ReactOS ARM64 Bootloader Build Script
# This script builds a minimal ARM64 bootloader image for testing

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}ReactOS ARM64 Bootloader Build Script${NC}"
echo "======================================"

# Check for ARM64 toolchain
# First try mingw32, then fallback to linux-gnu
TOOLCHAIN_PREFIX="aarch64-w64-mingw32"
TOOLCHAIN_FOUND=0

# Check if mingw32 toolchain has gcc
if command -v ${TOOLCHAIN_PREFIX}-gcc &> /dev/null; then
    TOOLCHAIN_FOUND=1
    echo -e "${GREEN}Found ARM64 MinGW toolchain${NC}"
elif [ -d "/home/ahmed/x-tools/aarch64-w64-mingw32/bin" ]; then
    # MinGW toolchain exists but might not have gcc, try with clang
    echo -e "${YELLOW}MinGW toolchain found but GCC missing, will try alternative configuration${NC}"
    TOOLCHAIN_FOUND=1
else
    # Fallback to Linux toolchain for testing
    TOOLCHAIN_PREFIX="aarch64-linux-gnu"
    if command -v ${TOOLCHAIN_PREFIX}-gcc &> /dev/null; then
        echo -e "${YELLOW}Warning: Using Linux ARM64 toolchain as fallback${NC}"
        echo -e "${YELLOW}This may not produce a proper PE/UEFI binary${NC}"
        TOOLCHAIN_FOUND=1
    fi
fi

if [ $TOOLCHAIN_FOUND -eq 0 ]; then
    echo -e "${RED}Error: No ARM64 toolchain found!${NC}"
    echo "Please install either:"
    echo "  - aarch64-w64-mingw32 toolchain (preferred)"
    echo "  - aarch64-linux-gnu toolchain (for testing)"
    exit 1
fi

# Set environment variables
export ROS_ARCH=arm64
export PATH="/home/ahmed/x-tools/aarch64-w64-mingw32/bin:$PATH"

# Source directory
SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SOURCE_DIR}/output-MinGW-arm64"

echo -e "${YELLOW}Configuration:${NC}"
echo "  Source: ${SOURCE_DIR}"
echo "  Build:  ${BUILD_DIR}"
echo "  Arch:   ${ROS_ARCH}"
echo ""

# Create build directory if it doesn't exist
if [ ! -d "${BUILD_DIR}" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir -p "${BUILD_DIR}"
fi

# Configure if needed
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo -e "${YELLOW}Configuring build system...${NC}"
    cd "${BUILD_DIR}"
    cmake -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE="${SOURCE_DIR}/toolchain-gcc.cmake" \
          -DARCH=arm64 \
          -DENABLE_CCACHE=OFF \
          -DENABLE_ROSTESTS=OFF \
          -DENABLE_ROSAPPS=OFF \
          -DUSE_CLANG_CL=OFF \
          "${SOURCE_DIR}"
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}Configuration failed!${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}Build system already configured${NC}"
fi

# Build the bootloader
echo -e "${YELLOW}Building ARM64 bootloader...${NC}"
cd "${BUILD_DIR}"

# First build the freeldr target (UEFI bootloader)
echo "Building uefildr..."
ninja uefildr

if [ $? -ne 0 ]; then
    echo -e "${RED}Bootloader build failed!${NC}"
    exit 1
fi

# Build the arm64img target (bootloader image)
echo "Building arm64img..."
ninja arm64img

if [ $? -ne 0 ]; then
    echo -e "${RED}Image creation failed!${NC}"
    echo "Note: Image creation requires mtools (mcopy, mmd) and mkfs.vfat"
    echo "Install with: sudo apt-get install mtools dosfstools"
    exit 1
fi

echo -e "${GREEN}Build completed successfully!${NC}"
echo ""
echo "Output files:"
echo "  UEFI Bootloader: ${BUILD_DIR}/boot/freeldr/freeldr/uefildr.efi"
echo "  Disk Image:      ${BUILD_DIR}/boot/freeldr/freeldr/arm64boot.img"
echo "  ISO Image:       ${BUILD_DIR}/boot/freeldr/freeldr/arm64boot.iso (if mkisofs available)"
echo ""
echo "To test with QEMU:"
echo "  1. Install QEMU ARM64: sudo apt-get install qemu-system-arm"
echo "  2. Install UEFI firmware: sudo apt-get install qemu-efi-aarch64"
echo "  3. Run: ninja -C ${BUILD_DIR} arm64img-qemu"
echo ""
echo "Or manually with:"
echo "  qemu-system-aarch64 -M virt -cpu cortex-a72 -m 2048 \\"
echo "    -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \\"
echo "    -drive file=${BUILD_DIR}/boot/freeldr/freeldr/arm64boot.img,format=raw,if=virtio \\"
echo "    -nographic -serial mon:stdio"