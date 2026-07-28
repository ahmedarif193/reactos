#!/bin/bash
#
# make_vhdx.sh — Create a VHDX rootfs from a WSL .tar/.wsl archive
#
# Usage: ./make_vhdx.sh [path-to-wsl-archive]
#   If no argument given, uses WSL_ARCHIVE below.
#
# Output: VHDX_OUTPUT (placed in roslinux/)
#

set -euo pipefail

# ---- Configuration ----
WSL_ARCHIVE="${1:-/home/ahmed/Downloads/Ubuntu2404-250130_x64.wsl}"
ROSLINUX_DIR="$(cd "$(dirname "$0")/../../roslinux" && pwd)"
VHDX_OUTPUT="${ROSLINUX_DIR}/ubuntu24.vhdx"
VHDX_SIZE_MB=2048       # Virtual disk size
EXT4_LABEL="cloudimg-rootfs"

# ---- Derived ----
RAW_IMG="$(mktemp /tmp/rosv-rootfs-XXXXXX.raw)"
MNT_DIR="$(mktemp -d /tmp/rosv-mnt-XXXXXX)"
INNER_TAR=""

cleanup() {
    set +e
    sudo umount "$MNT_DIR" 2>/dev/null
    rmdir "$MNT_DIR" 2>/dev/null
    [ -f "$RAW_IMG" ] && rm -f "$RAW_IMG"
    [ -n "$INNER_TAR" ] && [ -f "$INNER_TAR" ] && rm -f "$INNER_TAR"
    set -e
}
trap cleanup EXIT

echo "=== ROSV VHDX Builder ==="
echo "  WSL archive : $WSL_ARCHIVE"
echo "  Output      : $VHDX_OUTPUT"
echo "  Size        : ${VHDX_SIZE_MB}MB"
echo ""

# ---- Validate input ----
if [ ! -f "$WSL_ARCHIVE" ]; then
    echo "ERROR: WSL archive not found: $WSL_ARCHIVE"
    exit 1
fi

# ---- Check tools ----
for tool in qemu-img mkfs.ext4 tar; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: Required tool '$tool' not found. Install it first."
        exit 1
    fi
done

# ---- Extract inner rootfs tar if WSL archive is a gzip containing a tar ----
# WSL .wsl files are gzip-compressed and may contain either:
#   a) A direct rootfs tar
#   b) A tar containing install.tar.gz (the actual rootfs)
echo "[1/5] Inspecting WSL archive..."

FILETYPE="$(file -b "$WSL_ARCHIVE")"
if echo "$FILETYPE" | grep -qi "gzip"; then
    # Check if it's a gzip of a tar that contains install.tar.gz
    INNER_CONTENTS="$(tar tzf "$WSL_ARCHIVE" 2>/dev/null | head -20 || true)"
    if echo "$INNER_CONTENTS" | grep -q "install.tar.gz"; then
        echo "  Found install.tar.gz inside archive, extracting..."
        INNER_TAR="$(mktemp /tmp/rosv-install-XXXXXX.tar.gz)"
        tar xzf "$WSL_ARCHIVE" -C /tmp install.tar.gz
        mv /tmp/install.tar.gz "$INNER_TAR"
        ROOTFS_TAR="$INNER_TAR"
    else
        echo "  Archive is a direct gzip rootfs tar."
        ROOTFS_TAR="$WSL_ARCHIVE"
    fi
else
    echo "  Archive is a plain tar."
    ROOTFS_TAR="$WSL_ARCHIVE"
fi

# ---- Create raw disk image ----
echo "[2/5] Creating ${VHDX_SIZE_MB}MB raw image..."
dd if=/dev/zero of="$RAW_IMG" bs=1M count=0 seek="$VHDX_SIZE_MB" 2>/dev/null
mkfs.ext4 -q -L "$EXT4_LABEL" -F "$RAW_IMG"

# ---- Mount and extract rootfs ----
echo "[3/5] Extracting rootfs..."
sudo mount -o loop "$RAW_IMG" "$MNT_DIR"
sudo tar xzf "$ROOTFS_TAR" -C "$MNT_DIR" --no-same-owner 2>/dev/null || \
    sudo tar xf "$ROOTFS_TAR" -C "$MNT_DIR" --no-same-owner 2>/dev/null
sync

# ---- Configure for ROSV serial console ----
echo "[3.5/5] Configuring for ROSV (serial console, network, user)..."

# Enable serial console getty
if [ -d "$MNT_DIR/etc/systemd/system" ]; then
    sudo mkdir -p "$MNT_DIR/etc/systemd/system/getty.target.wants"
    sudo ln -sf /lib/systemd/system/serial-getty@.service \
        "$MNT_DIR/etc/systemd/system/getty.target.wants/serial-getty@ttyS0.service"
fi

# Set hostname
echo "rosv-vm" | sudo tee "$MNT_DIR/etc/hostname" > /dev/null

# Configure DNS
sudo mkdir -p "$MNT_DIR/etc"
printf "nameserver 10.0.2.3\n" | sudo tee "$MNT_DIR/etc/resolv.conf" > /dev/null

# Configure systemd-networkd for DHCP (WSL tar has no network config)
sudo mkdir -p "$MNT_DIR/etc/systemd/network"
cat <<'NETCFG' | sudo tee "$MNT_DIR/etc/systemd/network/20-wired.network" > /dev/null
[Match]
Name=eth* en* ens*
[Network]
DHCP=yes
NETCFG
# Enable systemd-networkd
sudo mkdir -p "$MNT_DIR/etc/systemd/system/multi-user.target.wants"
sudo ln -sf /lib/systemd/system/systemd-networkd.service \
    "$MNT_DIR/etc/systemd/system/multi-user.target.wants/systemd-networkd.service"
sudo mkdir -p "$MNT_DIR/etc/systemd/system/sockets.target.wants"
sudo ln -sf /lib/systemd/system/systemd-networkd.socket \
    "$MNT_DIR/etc/systemd/system/sockets.target.wants/systemd-networkd.socket"

# Ensure a default user exists (wsluser with no password, sudo access)
if ! sudo grep -q "^wsluser:" "$MNT_DIR/etc/passwd" 2>/dev/null; then
    sudo chroot "$MNT_DIR" useradd -m -s /bin/bash -G sudo wsluser 2>/dev/null || true
fi
# Passwordless login (empty password hash)
sudo chroot "$MNT_DIR" passwd -d wsluser 2>/dev/null || true
# Passwordless sudo
echo "wsluser ALL=(ALL) NOPASSWD: ALL" | sudo tee "$MNT_DIR/etc/sudoers.d/wsluser" > /dev/null
sudo chmod 440 "$MNT_DIR/etc/sudoers.d/wsluser"

# Auto-login on serial console (ttyS0 — used by rosl PTY)
OVERRIDE_DIR="$MNT_DIR/etc/systemd/system/serial-getty@ttyS0.service.d"
sudo mkdir -p "$OVERRIDE_DIR"
cat <<'UNIT' | sudo tee "$OVERRIDE_DIR/autologin.conf" > /dev/null
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin wsluser --noclear %I 115200 linux
UNIT

# Auto-login on getty@tty1 (template unit, %I = tty1)
OVERRIDE_DIR="$MNT_DIR/etc/systemd/system/getty@tty1.service.d"
sudo mkdir -p "$OVERRIDE_DIR"
cat <<'UNIT' | sudo tee "$OVERRIDE_DIR/autologin.conf" > /dev/null
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin wsluser --noclear %I $TERM
UNIT

# Auto-login on console-getty (uses /dev/console directly, not a template)
OVERRIDE_DIR="$MNT_DIR/etc/systemd/system/console-getty.service.d"
sudo mkdir -p "$OVERRIDE_DIR"
cat <<'UNIT' | sudo tee "$OVERRIDE_DIR/autologin.conf" > /dev/null
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin wsluser --noclear - linux
UNIT

# Disable heavy boot services that cause disk I/O contention in the VM
for SVC in \
    snapd.service snapd.socket snapd.seeded.service snap.lxd.activate.service \
    unattended-upgrades.service apt-daily.timer apt-daily-upgrade.timer \
    apport.service apport-autoreport.service \
    motd-news.timer fwupd.service fwupd-refresh.timer \
    ua-timer.timer ubuntu-advantage.service \
    multipathd.service multipathd.socket \
    ModemManager.service networkd-dispatcher.service \
    cloud-init.service cloud-init-local.service cloud-config.service cloud-final.service; do
    sudo ln -sf /dev/null "$MNT_DIR/etc/systemd/system/$SVC" 2>/dev/null || true
done

# Allow empty password login via PAM
if [ -f "$MNT_DIR/etc/pam.d/common-auth" ]; then
    sudo sed -i 's/pam_unix.so/pam_unix.so nullok/' "$MNT_DIR/etc/pam.d/common-auth" 2>/dev/null || true
fi

echo "  Rootfs size: $(sudo du -sh "$MNT_DIR" 2>/dev/null | cut -f1)"
sudo umount "$MNT_DIR"

# ---- Convert to VHDX ----
echo "[4/5] Converting to VHDX..."

# Backup existing VHDX
if [ -f "$VHDX_OUTPUT" ]; then
    BACKUP="${VHDX_OUTPUT}.bak.$(date +%Y%m%d_%H%M%S)"
    echo "  Backing up existing VHDX -> $(basename "$BACKUP")"
    sudo mv "$VHDX_OUTPUT" "$BACKUP"
fi

qemu-img convert -f raw -O vhdx -o subformat=dynamic "$RAW_IMG" "$VHDX_OUTPUT"

echo "[5/5] Done!"
echo ""
echo "  VHDX: $VHDX_OUTPUT ($(du -h "$VHDX_OUTPUT" | cut -f1))"
echo ""
echo "  To use: rosl.exe --disk $VHDX_OUTPUT"
