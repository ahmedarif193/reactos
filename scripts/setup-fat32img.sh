#!/bin/bash
set -e

if [[ "$(uname)" == "Darwin" ]]; then
    # macOS (Darwin) path

    # Cleanup: detach any existing attachment and remove old file
    if [[ -f fat32.img ]]; then
        # Try to find and detach any disk attached to this image
        EXISTING=$(hdiutil info 2>/dev/null | grep -B 10 "fat32.img" | grep "/dev/disk" | head -1 | awk '{print $1}') || true
        if [[ -n "$EXISTING" ]]; then
            echo "Detaching existing attachment: $EXISTING"
            hdiutil detach "$EXISTING" -force 2>/dev/null || true
            sleep 1
        fi
        rm -f fat32.img
    fi

    # Create raw image
    qemu-img create -f raw fat32.img 256M

    # Attach raw image as a disk device
    DISK=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount fat32.img | awk '{print $1}')
    echo "Attached as $DISK"

    # Create MBR partition table with one FAT32 partition and format it
    # MBR = Master Boot Record, MS-DOS FAT32 = FAT32 filesystem
    diskutil partitionDisk "$DISK" 1 MBR "MS-DOS FAT32" USBDISK 100%

    # Copy files from ./samples if it exists and has content
    # Check current dir first, then script-relative path
    if [[ -d "./samples" ]]; then
        SAMPLES_DIR="./samples"
    else
        SAMPLES_DIR="$(dirname "$0")/../samples"
    fi
    if [[ -d "$SAMPLES_DIR" ]] && [[ -n "$(ls -A "$SAMPLES_DIR" 2>/dev/null)" ]]; then
        echo "Copying files from $SAMPLES_DIR ..."
        cp -r "$SAMPLES_DIR"/* /Volumes/USBDISK/
        echo "Files copied:"
        ls -la /Volumes/USBDISK/
    fi

    # Detach
    hdiutil detach "$DISK"

else
    # Linux path

    rm -f fat32.img
    qemu-img create -f raw fat32.img 256M

    # Create an MBR partition table with one primary FAT32 partition
    parted -s fat32.img mklabel msdos
    parted -s fat32.img mkpart primary fat32 1MiB 100%
    parted -s fat32.img set 1 lba on

    # Attach a loop device with partition scanning
    LOOP=$(sudo losetup --find --show --partscan fat32.img)

    # Format partition 1 as FAT32
    sudo mkfs.vfat -F 32 -n USBDISK ${LOOP}p1

    # Copy files from ./samples if it exists and has content
    # Check current dir first, then script-relative path
    if [[ -d "./samples" ]]; then
        SAMPLES_DIR="./samples"
    else
        SAMPLES_DIR="$(dirname "$0")/../samples"
    fi
    if [[ -d "$SAMPLES_DIR" ]] && [[ -n "$(ls -A "$SAMPLES_DIR" 2>/dev/null)" ]]; then
        echo "Copying files from $SAMPLES_DIR ..."
        MOUNT_POINT=$(mktemp -d)
        sudo mount ${LOOP}p1 "$MOUNT_POINT"
        sudo cp -r "$SAMPLES_DIR"/* "$MOUNT_POINT"/
        echo "Files copied:"
        ls -la "$MOUNT_POINT"/
        sudo umount "$MOUNT_POINT"
        rmdir "$MOUNT_POINT"
    fi

    # Detach
    sudo losetup -d "$LOOP"
fi


# # Get byte offset of partition 1
# OFFSET=$(parted -s fat32.img unit B print | awk '/ 1 /{gsub("B","",$2); print $2}')

# # Copy using mtools with offset
# mcopy -i fat32.img@@$OFFSET somefile.txt ::/

# qemu-system-x86_64 -enable-kvm \
# -drive file=livecd_sprint-6.4-fix-q35-ICH9.iso \
# -serial file:/tmp/v.log -m 3G -M q35\
#  -netdev user,id=net0 -device e1000,netdev=net0 -drive if=none,id=usbdisk,file=fat32.img \
#  -device ich9-usb-ehci1,id=ehci -device usb-storage,drive=usbdisk \
#  -device usb-tablet,bus=ehci.0

# qemu-system-x86_64 -enable-kvm \
#   -M q35 \
#   -m 3G \                                       
#   -drive file=livecd_sprint-6.4-fix-q35-ICH9.iso \
#   -device qemu-xhci,id=xhci \                                                             
#   -drive if=none,id=usbdisk,file=fat32.img \                       
#   -device usb-storage,drive=usbdisk \
#   -serial file:/tmp/v.log \
#   -device usb-kbd \
#   -device usb-tablet
