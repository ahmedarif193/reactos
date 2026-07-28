#!/bin/bash
#
# make_ubuntu_vhdx.sh - Build Ubuntu WSL VHDX directly from sysroot
#
# Downloads the WSL rootfs, formats ext4 on a dynamic VHDX via qemu-nbd,
# and populates it from the extracted sysroot. No intermediate raw image.
#
# Requires: sudo, qemu-img, qemu-nbd, mkfs.ext4, tune2fs, curl

set -euo pipefail

ROOTFS_URL="https://cloud-images.ubuntu.com/wsl/mantic/current/ubuntu-mantic-wsl-amd64-wsl.rootfs.tar.gz"
DEFAULT_USER="wsluser"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROSLINUX_DIR="$(realpath "$SCRIPT_DIR/../../roslinux")"
OUTPUT="$ROSLINUX_DIR/ubuntu24.vhdx"
SIZE_GB=16

log() { echo "[make-vhdx] $*"; }
die() { echo "[make-vhdx:ERROR] $*" >&2; exit 1; }

[[ $EUID -ne 0 ]] && die "Run as root: sudo $0"
for t in qemu-img qemu-nbd mkfs.ext4 tune2fs curl; do
    command -v "$t" &>/dev/null || die "Missing: $t"
done
mkdir -p "$ROSLINUX_DIR"

# ---- Work area --------------------------------------------------------------
WORK=$(mktemp -d /tmp/make-vhdx.XXXXXX)
SYSROOT="$WORK/sysroot"
MNT="$WORK/mnt"
mkdir -p "$SYSROOT" "$MNT"

NBD_DEV=""
cleanup() {
    mountpoint -q "$MNT" 2>/dev/null && umount "$MNT" || true
    [[ -n "$NBD_DEV" ]] && qemu-nbd --disconnect "$NBD_DEV" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

# ---- 1. Download + extract sysroot ------------------------------------------
log "[1/4] Downloading Ubuntu WSL rootfs..."
TAR="$WORK/rootfs.tar.gz"
curl -L --progress-bar -o "$TAR" "$ROOTFS_URL"
log "      $(du -h "$TAR" | cut -f1) downloaded"

log "      Extracting sysroot..."
tar xf "$TAR" -C "$SYSROOT"
log "      $(find "$SYSROOT" -type f | wc -l) files"

# Do not derive kernel modules from local initrd in this builder.
# Keep the image input deterministic: only the downloaded WSL rootfs plus
# explicit ROSV config in this script.
mkdir -p "$SYSROOT/etc/modules-load.d"
printf 'virtio_blk\nvirtio_net\n' > "$SYSROOT/etc/modules-load.d/virtio.conf"

# ---- 2. Configure for ROSV --------------------------------------------------
log "      Configuring for ROSV serial-console boot..."

echo "rosv-vm" > "$SYSROOT/etc/hostname"
cat > "$SYSROOT/etc/hosts" << 'EOF'
127.0.0.1   localhost
127.0.1.1   rosv-vm
::1         localhost ip6-localhost ip6-loopback
EOF

cat > "$SYSROOT/etc/fstab" << 'EOF'
/dev/vda  /     ext4  defaults,noatime,errors=remount-ro  0 1
proc      /proc proc  defaults                             0 0
sysfs     /sys  sysfs defaults                             0 0
tmpfs     /tmp  tmpfs defaults,nosuid,nodev                0 0
EOF

mkdir -p "$SYSROOT/etc/systemd/system/serial-getty@ttyS0.service.d"
cat > "$SYSROOT/etc/systemd/system/serial-getty@ttyS0.service.d/autologin.conf" << 'EOF'
[Service]
Type=simple
ExecStart=
ExecStart=-/sbin/agetty --autologin wsluser --local-line --keep-baud 115200,57600,38400,9600 --noissue --noclear %I $TERM
EOF
mkdir -p "$SYSROOT/etc/systemd/system/getty.target.wants"
ln -sf /lib/systemd/system/serial-getty@.service \
    "$SYSROOT/etc/systemd/system/getty.target.wants/serial-getty@ttyS0.service"

# Keep a single login surface in ROSV headless mode.
# Running both console-getty and serial-getty against ttyS0 causes noisy
# re-login churn and duplicate sessions.
ln -sf /dev/null "$SYSROOT/etc/systemd/system/console-getty.service"
ln -sf /dev/null "$SYSROOT/etc/systemd/system/getty@tty1.service"

# Mask WSL-specific units that stall on a plain kernel
for u in wsl-binfmt.service wsl-pro.service wsl-pro-service.service \
          proc-sys-fs-binfmt_misc.automount systemd-binfmt.service \
          snapd.service snapd.socket; do
    ln -sf /dev/null "$SYSROOT/etc/systemd/system/$u" 2>/dev/null || true
done

mkdir -p "$SYSROOT/etc/systemd/network"
cat > "$SYSROOT/etc/systemd/network/20-wired.network" << 'EOF'
[Match]
Name=eth* en* ens*
[Network]
DHCP=yes
EOF

# Ensure the DHCP policy above is actually applied on boot.
mkdir -p "$SYSROOT/etc/systemd/system/multi-user.target.wants"
ln -sf /lib/systemd/system/systemd-networkd.service \
    "$SYSROOT/etc/systemd/system/multi-user.target.wants/systemd-networkd.service"

# Keep DNS plumbing aligned with networkd-managed links.
ln -sf /lib/systemd/system/systemd-resolved.service \
    "$SYSROOT/etc/systemd/system/multi-user.target.wants/systemd-resolved.service"
ln -sfn /run/systemd/resolve/stub-resolv.conf "$SYSROOT/etc/resolv.conf"

echo "root:root" | chroot "$SYSROOT" chpasswd 2>/dev/null || true

if ! chroot "$SYSROOT" id -u "$DEFAULT_USER" >/dev/null 2>&1; then
    chroot "$SYSROOT" useradd -m -s /bin/bash -G sudo "$DEFAULT_USER" 2>/dev/null || true
fi
chroot "$SYSROOT" passwd -d "$DEFAULT_USER" >/dev/null 2>&1 || true
chroot "$SYSROOT" usermod -aG sudo "$DEFAULT_USER" >/dev/null 2>&1 || true
chroot "$SYSROOT" passwd -l root >/dev/null 2>&1 || true

mkdir -p "$SYSROOT/etc/sudoers.d"
cat > "$SYSROOT/etc/sudoers.d/90-$DEFAULT_USER-nopasswd" << EOF
$DEFAULT_USER ALL=(ALL) NOPASSWD:ALL
EOF
chmod 0440 "$SYSROOT/etc/sudoers.d/90-$DEFAULT_USER-nopasswd"

cat > "$SYSROOT/etc/wsl.conf" << EOF
[user]
default=$DEFAULT_USER
EOF

mkdir -p "$SYSROOT"/{proc,sys,dev,run,tmp,mnt}
chmod 1777 "$SYSROOT/tmp"

# ---- 3. Create VHDX + format ext4 directly ---------------------------------
log "[2/4] Creating ${SIZE_GB}G dynamic VHDX: $OUTPUT"
rm -f "$OUTPUT"
qemu-img create -f vhdx -o subformat=dynamic "$OUTPUT" "${SIZE_GB}G"

log "[3/4] Connecting via qemu-nbd..."
modprobe nbd max_part=0

for dev in /dev/nbd{0..15}; do
    sz=$(blockdev --getsize64 "$dev" 2>/dev/null || echo 0)
    [[ "$sz" -eq 0 ]] && { NBD_DEV="$dev"; break; }
done
[[ -z "$NBD_DEV" ]] && die "No free nbd device"

qemu-nbd --connect="$NBD_DEV" "$OUTPUT"
sleep 1

log "      mkfs.ext4 on $NBD_DEV..."
mkfs.ext4 -F -q \
    -L "rosv-root" \
    -O "^metadata_csum,^64bit" \
    -E "lazy_itable_init=0,lazy_journal_init=0" \
    "$NBD_DEV"
tune2fs -c 0 -i 0 "$NBD_DEV" &>/dev/null || true

log "      Populating sysroot..."
mount "$NBD_DEV" "$MNT"
cp -a "$SYSROOT"/. "$MNT"/
sync
USED=$(df -BM "$MNT" | awk 'NR==2{print $3}')
umount "$MNT"

log "[4/4] Disconnecting nbd..."
qemu-nbd --disconnect "$NBD_DEV"
NBD_DEV=""

log ""
log "=== Done ==="
log "  Output : $OUTPUT"
log "  Used   : $USED / ${SIZE_GB}G virtual"
log "  On disk: $(du -h "$OUTPUT" | cut -f1)"
