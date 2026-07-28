#!/bin/sh
#
# download_linux.sh - Download Alpine Linux netboot kernel + initrd
#
# Downloads vmlinuz-lts and initramfs-lts from the Alpine CDN.
# The initramfs already contains busybox with a shell, suitable
# for booting to a console under the rosv hypervisor.
#
# Usage: ./download_linux.sh [output_dir]
#   output_dir defaults to <repo>/roslinux/

set -e

ALPINE_VERSION="3.21"
ALPINE_ARCH="x86_64"
BASE_URL="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION}/releases/${ALPINE_ARCH}/netboot"

KERNEL_URL="${BASE_URL}/vmlinuz-lts"
INITRD_URL="${BASE_URL}/initramfs-lts"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(realpath "$SCRIPT_DIR/../..")"
OUT_DIR="${1:-$REPO_ROOT/roslinux}"

# Pick a download tool
if command -v curl >/dev/null 2>&1; then
    DL="curl -fSL -o"
elif command -v wget >/dev/null 2>&1; then
    DL="wget -q -O"
else
    echo "ERROR: curl or wget is required" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

echo "Downloading Alpine v${ALPINE_VERSION} ${ALPINE_ARCH} netboot files..."

echo "  vmlinuz-lts -> ${OUT_DIR}/vmlinuz"
$DL "${OUT_DIR}/vmlinuz" "$KERNEL_URL"

echo "  initramfs-lts -> ${OUT_DIR}/initrd.img"
$DL "${OUT_DIR}/initrd.img" "$INITRD_URL"

echo "Done. Files saved to ${OUT_DIR}/"
ls -lh "${OUT_DIR}/vmlinuz" "${OUT_DIR}/initrd.img"
