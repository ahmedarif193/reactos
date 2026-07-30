#!/bin/sh
# Rebuild the UEFI network drivers this repository ships for HTTP boot.
#
# Usage: rebuild.sh /path/to/edk2 [X64|AARCH64]
#   The edk2 checkout should be at (or near) edk2-stable202605 with BaseTools
#   built (make -C BaseTools) and submodules initialized. AARCH64 also needs an
#   aarch64 cross toolchain (GCC_AARCH64_PREFIX, default aarch64-linux-gnu-).
#
# Copy the resulting binaries into media/boot/uefi_drivers/<arch>/ and update
# the SHA-256 hashes in THIRDPARTY_NOTICES.txt.
set -e

EDK2="${1:?usage: rebuild.sh /path/to/edk2 [X64|AARCH64]}"
TARGET_ARCH="${2:-X64}"
PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$PATCH_DIR/../src" && pwd)"

cd "$EDK2"
git apply --check "$PATCH_DIR/edk2-networkpkg-httpboot.patch" 2>/dev/null && \
    git apply "$PATCH_DIR/edk2-networkpkg-httpboot.patch" || \
    echo "Patch already applied (or does not apply cleanly); continuing."

export WORKSPACE="$PWD"
export PACKAGES_PATH="$PWD:$SRC_DIR"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
. ./edksetup.sh

# All PCDs must resolve FixedAtBuild (BasePcdLibNull asserts at runtime and a
# foreign firmware's dynamic PCD database uses different token numbers).
# Verify every [Pcd] entry of the module says FIXED in the -y report.
build -a "$TARGET_ARCH" -t GCC -p NetworkPkg/NetworkPkg.dsc -b RELEASE \
    -m NetworkPkg/HttpDxe/HttpDxe.inf \
    --pcd gEfiNetworkPkgTokenSpaceGuid.PcdAllowHttpConnections=TRUE \
    -y "$WORKSPACE/Build/httpdxe-report.txt"

if [ "$TARGET_ARCH" = "X64" ]; then
    # Stretched ACKs: only worth shipping where we replace the whole firmware
    # network stack, which the LattePanda Mu firmware does not provide at all.
    build -a "$TARGET_ARCH" -t GCC -p NetworkPkg/NetworkPkg.dsc -b RELEASE \
        -m NetworkPkg/TcpDxe/TcpDxe.inf \
        -y "$WORKSPACE/Build/tcpdxe-report.txt"
else
    # The Raspberry Pi 5 firmware carries the network stack but no driver for
    # the board's own Ethernet MAC.
    build -a "$TARGET_ARCH" -t GCC -p Rp1GemPkg/Rp1GemPkg.dsc -b RELEASE \
        -m Rp1GemPkg/Rp1GemDxe/Rp1GemDxe.inf
fi

echo "Binaries under $WORKSPACE/Build/*/RELEASE_GCC/$TARGET_ARCH/"
grep -E "FIXED|DYNAMIC|PATCHABLE" "$WORKSPACE/Build/httpdxe-report.txt" | head -8
