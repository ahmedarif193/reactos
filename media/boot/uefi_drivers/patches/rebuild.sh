#!/bin/sh
# Rebuild the locally patched EDK2 network drivers for LattePanda Mu HTTP boot.
#
# Usage: rebuild.sh /path/to/edk2
#   The edk2 checkout should be at (or near) edk2-stable202605 with BaseTools
#   built (make -C BaseTools) and submodules initialized.
#
# Produces HttpDxe.efi and TcpDxe.efi under Build/NetworkPkg/RELEASE_GCC/X64/;
# copy them into media/boot/uefi_drivers/amd64/ and update the
# SHA-256 hashes in THIRDPARTY_NOTICES.txt.
set -e

EDK2="${1:?usage: rebuild.sh /path/to/edk2}"
PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$EDK2"
git apply --check "$PATCH_DIR/edk2-networkpkg-httpboot.patch" 2>/dev/null && \
    git apply "$PATCH_DIR/edk2-networkpkg-httpboot.patch" || \
    echo "Patch already applied (or does not apply cleanly); continuing."

export WORKSPACE="$PWD"
. ./edksetup.sh

# All PCDs must resolve FixedAtBuild (BasePcdLibNull asserts at runtime and a
# foreign firmware's dynamic PCD database uses different token numbers).
# Verify every [Pcd] entry of the module says FIXED in the -y report.
build -a X64 -t GCC -p NetworkPkg/NetworkPkg.dsc -b RELEASE \
    -m NetworkPkg/HttpDxe/HttpDxe.inf \
    --pcd gEfiNetworkPkgTokenSpaceGuid.PcdAllowHttpConnections=TRUE \
    -y "$WORKSPACE/Build/httpdxe-report.txt"

build -a X64 -t GCC -p NetworkPkg/NetworkPkg.dsc -b RELEASE \
    -m NetworkPkg/TcpDxe/TcpDxe.inf \
    -y "$WORKSPACE/Build/tcpdxe-report.txt"

echo "Binaries: $WORKSPACE/Build/NetworkPkg/RELEASE_GCC/X64/{HttpDxe,TcpDxe}.efi"
grep -E "FIXED|DYNAMIC|PATCHABLE" "$WORKSPACE/Build/httpdxe-report.txt" | head -8
