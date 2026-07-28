#!/bin/bash
#
# make_rootfs.sh - Create an ext4 disk image from a cpio-format rootfs
#
# Takes the full Ubuntu rootfs (currently packed as a 400MB gzipped cpio
# inside initrd.img) and writes it into a raw ext4 disk image file.
# This image is then served to the guest kernel via virtio-blk as /dev/vda.
#
# Usage: ./make_rootfs.sh [options]
#   --input <path>    Input cpio.gz rootfs (default: <repo>/roslinux/initrd.img)
#   --output <path>   Output ext4 raw image (default: <repo>/roslinux/rootfs.img)
#   --size <MB>       Image size in MB (default: 2048 = 2 GB)
#   --label <str>     Filesystem label (default: rootfs)
#
# Requirements: sudo (for mount), mkfs.ext4, cpio, gunzip

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(realpath "$SCRIPT_DIR/../..")"

# Defaults
INPUT=""
OUTPUT="$REPO_ROOT/roslinux/rootfs.img"
SIZE_MB=2048
LABEL="rootfs"

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --input)   INPUT="$2";   shift 2 ;;
        --output)  OUTPUT="$2";  shift 2 ;;
        --size)    SIZE_MB="$2"; shift 2 ;;
        --label)   LABEL="$2";   shift 2 ;;
        -h|--help)
            sed -n '2,/^$/s/^# //p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Find input cpio if not specified
if [ -z "$INPUT" ]; then
    if [ -f "$REPO_ROOT/roslinux/initrd.img" ]; then
        INPUT="$REPO_ROOT/roslinux/initrd.img"
    fi
fi

if [ -z "$INPUT" ] || [ ! -f "$INPUT" ]; then
    echo "ERROR: No input cpio.gz found. Specify with --input <path>" >&2
    exit 1
fi

# Check if input looks like a cpio
if ! file "$INPUT" | grep -q "gzip"; then
    echo "ERROR: $INPUT does not appear to be gzip compressed" >&2
    exit 1
fi

echo "=== Building ext4 rootfs image ==="
echo "  input:  $INPUT ($(du -h "$INPUT" | cut -f1))"
echo "  output: $OUTPUT"
echo "  size:   ${SIZE_MB} MB"
echo "  label:  $LABEL"
echo ""

# Check we have required tools
for tool in mkfs.ext4 cpio gunzip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: Required tool '$tool' not found" >&2
        exit 1
    fi
done

mkdir -p "$(dirname "$OUTPUT")"

# Create the raw image file
echo "[1/4] Creating ${SIZE_MB}MB raw image..."
dd if=/dev/zero of="$OUTPUT" bs=1M count="$SIZE_MB" status=progress 2>&1

# Format as ext4
echo ""
echo "[2/4] Formatting as ext4..."
mkfs.ext4 -L "$LABEL" -F -q "$OUTPUT"

# Mount and populate
MOUNTPOINT=$(mktemp -d /tmp/rootfs-mount.XXXXXX)
trap 'sudo umount "$MOUNTPOINT" 2>/dev/null; rmdir "$MOUNTPOINT" 2>/dev/null' EXIT

echo "[3/4] Mounting and extracting rootfs..."

# We need sudo for mount
if [ "$(id -u)" -ne 0 ]; then
    echo "  (sudo required for loopback mount)"
fi

sudo mount -o loop "$OUTPUT" "$MOUNTPOINT"

# Extract the cpio into the mounted filesystem
# This preserves ownership, permissions, device nodes, symlinks
(gunzip -c "$INPUT" | sudo cpio -id --quiet -D "$MOUNTPOINT") 2>&1

# Ensure essential directories exist
for dir in dev proc sys tmp run var/run; do
    sudo mkdir -p "$MOUNTPOINT/$dir"
done

# Set permissions on tmp
sudo chmod 1777 "$MOUNTPOINT/tmp"

# Show what we populated
echo ""
echo "[4/4] Verifying rootfs contents..."
echo "  Total files: $(sudo find "$MOUNTPOINT" -type f | wc -l)"
echo "  Total dirs:  $(sudo find "$MOUNTPOINT" -type d | wc -l)"
echo "  Used space:  $(sudo du -sh "$MOUNTPOINT" | cut -f1)"

# Check for init
for init_path in sbin/init init bin/sh; do
    if [ -e "$MOUNTPOINT/$init_path" ]; then
        echo "  init found:  /$init_path"
        break
    fi
done

# Check for key directories
echo "  Key paths:"
for p in etc/fstab etc/passwd bin/bash usr/bin/python3 sbin/init; do
    if [ -e "$MOUNTPOINT/$p" ]; then
        echo "    /$p  (present)"
    else
        echo "    /$p  (MISSING)"
    fi
done

# Unmount
sudo umount "$MOUNTPOINT"
rmdir "$MOUNTPOINT"

echo ""
echo "=== ext4 rootfs image built ==="
echo "  Output: $OUTPUT"
echo "  Size:   $(du -h "$OUTPUT" | cut -f1)"
echo ""
echo "Usage with rosl:"
echo "  rosl.exe vmlinuz initrd-virtio.img 512 \\"
echo "    'console=ttyS0,115200n8 root=/dev/vda rootfstype=ext4 rdinit=/init nokaslr'"
echo ""
echo "QEMU test:"
echo "  qemu-system-x86_64 -kernel vmlinuz -initrd initrd-virtio.img \\"
echo "    -drive file=$OUTPUT,format=raw,if=virtio \\"
echo "    -append 'console=ttyS0 root=/dev/vda rootfstype=ext4 rdinit=/init' \\"
echo "    -nographic -m 512"
