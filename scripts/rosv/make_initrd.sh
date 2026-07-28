#!/bin/bash
#
# make_initrd.sh - Build a minimal initramfs for virtio-blk root boot
#
# Creates a tiny initrd (<3 MB compressed) that:
#   1. Loads virtio_blk.ko (the WSL2 kernel has virtio core built-in but
#      virtio_blk as a module)
#   2. Waits for /dev/vda to appear
#   3. Mounts it as ext4
#   4. switch_root into it
#
# Usage: ./make_initrd.sh [options]
#   --kernel-build <dir>  Path to kernel build/source tree (has drivers/block/virtio_blk.ko)
#   --virtio-blk <path>   Direct path to virtio_blk.ko module
#   --kver <version>      Kernel version string (default: auto-detect from module)
#   --busybox <path>      Path to static busybox binary (default: /bin/busybox)
#   --output <path>       Output initrd path (default: <repo>/roslinux/initrd-virtio.img)
#   --rootdev <dev>       Root block device (default: /dev/vda)
#   --rootfstype <type>   Root filesystem type (default: ext4)
#
# The resulting initrd is suitable for booting the WSL2 kernel
# (6.6.114.1-microsoft-standard-WSL2+) which has:
#   CONFIG_VIRTIO=y          (built-in)
#   CONFIG_VIRTIO_PCI=y      (built-in)
#   CONFIG_VIRTIO_MMIO=y     (built-in)
#   CONFIG_VIRTIO_BLK=m      (MODULE - needs this initrd to load it)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(realpath "$SCRIPT_DIR/../..")"

# Defaults
BUSYBOX="/bin/busybox"
OUTPUT="$REPO_ROOT/roslinux/initrd-virtio.img"
ROOTDEV="/dev/vda"
ROOTFSTYPE="ext4"
KERNEL_BUILD=""
VIRTIO_BLK_PATH=""
KVER=""

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --kernel-build) KERNEL_BUILD="$2"; shift 2 ;;
        --virtio-blk)   VIRTIO_BLK_PATH="$2"; shift 2 ;;
        --kver)         KVER="$2"; shift 2 ;;
        --busybox)      BUSYBOX="$2"; shift 2 ;;
        --output)       OUTPUT="$2"; shift 2 ;;
        --rootdev)      ROOTDEV="$2"; shift 2 ;;
        --rootfstype)   ROOTFSTYPE="$2"; shift 2 ;;
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

# Validate busybox
if [ ! -x "$BUSYBOX" ]; then
    echo "ERROR: busybox not found at $BUSYBOX" >&2
    echo "Install with: apt install busybox-static" >&2
    exit 1
fi

if ! file "$BUSYBOX" | grep -q "statically linked"; then
    echo "WARNING: busybox at $BUSYBOX is not statically linked" >&2
    echo "The initrd may not work. Install busybox-static." >&2
fi

# Find virtio_blk.ko
if [ -z "$VIRTIO_BLK_PATH" ]; then
    # Try kernel build tree
    if [ -z "$KERNEL_BUILD" ]; then
        # Auto-detect: look in common locations
        for candidate in \
            "$HOME/WorkDir/wsl2-kernel" \
            "/usr/src/linux" \
            "$REPO_ROOT"; do
            if [ -f "$candidate/drivers/block/virtio_blk.ko" ]; then
                KERNEL_BUILD="$(realpath "$candidate")"
                break
            fi
        done
    fi

    if [ -n "$KERNEL_BUILD" ]; then
        VIRTIO_BLK_PATH="$KERNEL_BUILD/drivers/block/virtio_blk.ko"
    fi
fi

if [ -z "$VIRTIO_BLK_PATH" ] || [ ! -f "$VIRTIO_BLK_PATH" ]; then
    echo "ERROR: Cannot find virtio_blk.ko" >&2
    echo "Specify with --kernel-build <dir> or --virtio-blk <path>" >&2
    exit 1
fi

# Auto-detect kernel version from the module's vermagic
if [ -z "$KVER" ]; then
    KVER=$(strings "$VIRTIO_BLK_PATH" | grep '^vermagic=' | head -1 | sed 's/^vermagic=//;s/ .*//')
    if [ -z "$KVER" ]; then
        echo "ERROR: Cannot determine kernel version from $VIRTIO_BLK_PATH" >&2
        echo "Specify with --kver <version>" >&2
        exit 1
    fi
fi

# Strip the module (remove debug info) for smaller initrd
STRIPPED_MODULE=$(mktemp /tmp/virtio_blk.XXXXXX.ko)
if command -v strip >/dev/null 2>&1; then
    strip --strip-debug "$VIRTIO_BLK_PATH" -o "$STRIPPED_MODULE"
else
    cp "$VIRTIO_BLK_PATH" "$STRIPPED_MODULE"
fi

# Create temporary working directory
WORKDIR=$(mktemp -d /tmp/initrd-build.XXXXXX)
trap 'rm -rf "$WORKDIR" "$STRIPPED_MODULE"' EXIT

echo "Building minimal virtio initrd..."
echo "  busybox:     $BUSYBOX"
echo "  virtio_blk:  $VIRTIO_BLK_PATH"
echo "  kernel:      $KVER"
echo "  stripped:    $(du -h "$STRIPPED_MODULE" | cut -f1) (from $(du -h "$VIRTIO_BLK_PATH" | cut -f1))"
echo "  output:      $OUTPUT"
echo "  rootdev:     $ROOTDEV"
echo "  rootfstype:  $ROOTFSTYPE"

# Create initrd directory structure
mkdir -p "$WORKDIR"/{bin,sbin,dev,proc,sys,newroot,etc}
mkdir -p "$WORKDIR/lib/modules/$KVER/kernel/drivers/block"

# Install busybox
cp "$BUSYBOX" "$WORKDIR/bin/busybox"
chmod 755 "$WORKDIR/bin/busybox"

# Create essential busybox symlinks
for cmd in sh mount umount insmod modprobe mkdir mknod cat echo sleep \
           switch_root ls dmesg mdev; do
    ln -sf busybox "$WORKDIR/bin/$cmd"
done
for cmd in mount umount insmod modprobe switch_root mdev; do
    ln -sf ../bin/busybox "$WORKDIR/sbin/$cmd"
done

# Install the stripped virtio_blk module
cp "$STRIPPED_MODULE" "$WORKDIR/lib/modules/$KVER/kernel/drivers/block/virtio_blk.ko"

# Generate minimal module metadata
echo "kernel/drivers/block/virtio_blk.ko:" > "$WORKDIR/lib/modules/$KVER/modules.dep"
echo "alias virtio:d00000002v* virtio_blk" > "$WORKDIR/lib/modules/$KVER/modules.alias"

# Create the init script
cat > "$WORKDIR/init" << 'INITEOF'
#!/bin/sh
#
# Minimal init for virtio-blk root mount
# Loads virtio_blk module, mounts root device, switch_root
#

/bin/busybox --install -s
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

msg() { echo "initrd: $*"; echo "initrd: $*" > /dev/kmsg 2>/dev/null; }

msg "starting minimal virtio-blk init"

# Mount essential pseudo-filesystems
mount -t proc  proc  /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || mount -t tmpfs tmpfs /dev

# Parse kernel command line
ROOTDEV="%%ROOTDEV%%"
ROOTFSTYPE="%%ROOTFSTYPE%%"
INIT="/sbin/init"

for param in $(cat /proc/cmdline); do
    case "$param" in
        root=*)       ROOTDEV="${param#root=}" ;;
        rootfstype=*) ROOTFSTYPE="${param#rootfstype=}" ;;
        init=*)       INIT="${param#init=}" ;;
        rdinit=*)     ;; # that is us
    esac
done

msg "root=$ROOTDEV fstype=$ROOTFSTYPE init=$INIT"

# Load virtio_blk kernel module
KVER=$(uname -r)
MODPATH="/lib/modules/$KVER/kernel/drivers/block/virtio_blk.ko"

if [ -f "$MODPATH" ]; then
    msg "loading virtio_blk ($KVER)"
    if insmod "$MODPATH"; then
        msg "virtio_blk loaded"
    else
        msg "ERROR: insmod virtio_blk.ko failed ($?)"
    fi
else
    msg "WARNING: $MODPATH not found, trying modprobe"
    modprobe virtio_blk 2>/dev/null || msg "modprobe virtio_blk failed"
fi

# Wait for root block device
msg "waiting for $ROOTDEV..."
WAIT=0
MAX_WAIT=50  # 5 seconds (50 x 100ms)
while [ ! -b "$ROOTDEV" ] && [ $WAIT -lt $MAX_WAIT ]; do
    # Populate /dev via mdev if devtmpfs did not auto-create nodes
    mdev -s 2>/dev/null || true
    sleep 0.1
    WAIT=$((WAIT + 1))
done

if [ ! -b "$ROOTDEV" ]; then
    msg "ERROR: $ROOTDEV not found after ${MAX_WAIT}00ms"
    msg "block devices:"
    ls -la /dev/vd* /dev/sd* /dev/hd* 2>/dev/null || msg "(none)"
    msg "dropping to emergency shell"
    exec /bin/sh
fi

msg "$ROOTDEV ready (waited ${WAIT}00ms)"

# Mount root filesystem
msg "mounting $ROOTDEV ($ROOTFSTYPE) -> /newroot"
if ! mount -t "$ROOTFSTYPE" -o ro "$ROOTDEV" /newroot; then
    msg "ERROR: mount $ROOTDEV failed"
    msg "dropping to emergency shell"
    exec /bin/sh
fi

# Find init binary in the new root
if [ ! -x "/newroot${INIT}" ]; then
    msg "WARNING: $INIT missing in rootfs"
    for alt in /sbin/init /init /bin/sh; do
        if [ -x "/newroot${alt}" ]; then
            INIT="$alt"
            msg "using $INIT"
            break
        fi
    done
fi

# Unmount pseudo-filesystems before switch
umount /proc 2>/dev/null
umount /sys  2>/dev/null
umount /dev  2>/dev/null

# Hand off to real init
msg "switch_root -> $INIT"
exec switch_root /newroot "$INIT"

# Fallback
msg "FATAL: switch_root failed"
exec /bin/sh
INITEOF

# Substitute defaults into init script
sed -i "s|%%ROOTDEV%%|$ROOTDEV|g" "$WORKDIR/init"
sed -i "s|%%ROOTFSTYPE%%|$ROOTFSTYPE|g" "$WORKDIR/init"
chmod 755 "$WORKDIR/init"

# Build the cpio archive with fakeroot for correct ownership and device nodes
echo ""
echo "Creating initrd cpio archive..."

CPIO_SCRIPT=$(mktemp /tmp/cpio-build.XXXXXX.sh)
cat > "$CPIO_SCRIPT" << CPIOEOF
#!/bin/sh
cd "$WORKDIR"
mknod -m 622 dev/console c 5 1 2>/dev/null || true
mknod -m 666 dev/null    c 1 3 2>/dev/null || true
mknod -m 666 dev/zero    c 1 5 2>/dev/null || true
mknod -m 444 dev/urandom c 1 9 2>/dev/null || true
chown -R 0:0 . 2>/dev/null || true
find . -print0 | cpio --null -o -H newc --quiet
CPIOEOF
chmod +x "$CPIO_SCRIPT"

mkdir -p "$(dirname "$OUTPUT")"
if command -v fakeroot >/dev/null 2>&1; then
    fakeroot "$CPIO_SCRIPT" | gzip -9 > "$OUTPUT"
elif [ "$(id -u)" -eq 0 ]; then
    "$CPIO_SCRIPT" | gzip -9 > "$OUTPUT"
else
    echo "  NOTE: no fakeroot/root, device nodes omitted (devtmpfs provides them)"
    (cd "$WORKDIR" && find . -print0 | cpio --null -o -H newc --quiet) | gzip -9 > "$OUTPUT"
fi
rm -f "$CPIO_SCRIPT"

# Report
echo ""
echo "=== Minimal virtio initrd built ==="
echo "  Output:  $OUTPUT"
echo "  Size:    $(du -h "$OUTPUT" | cut -f1)"
echo ""
echo "Contents:"
(cd "$WORKDIR" && find . -type f | sort | while read f; do
    printf "  %-65s %s\n" "$f" "$(ls -lh "$f" | awk '{print $5}')"
done)
echo ""
echo "Kernel cmdline:"
echo "  root=$ROOTDEV rootfstype=$ROOTFSTYPE rdinit=/init"
