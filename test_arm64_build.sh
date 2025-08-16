#!/bin/bash

# Create a test directory for the ARM64 UEFI boot
TESTDIR=/tmp/reactos-arm64-test
rm -rf $TESTDIR
mkdir -p $TESTDIR

# Create a FAT32 EFI system partition image
dd if=/dev/zero of=$TESTDIR/uefi.img bs=1M count=64
mkfs.vfat -F 32 $TESTDIR/uefi.img

# Mount the image and copy the bootloader
mkdir -p $TESTDIR/mnt
sudo mount -o loop $TESTDIR/uefi.img $TESTDIR/mnt
sudo mkdir -p $TESTDIR/mnt/EFI/BOOT
sudo cp /home/ahmed/WorkDir/reactos/output-MinGW-arm64/boot/freeldr/freeldr/uefildr.efi $TESTDIR/mnt/EFI/BOOT/BOOTAA64.EFI

# Create a simple freeldr.ini configuration
sudo tee $TESTDIR/mnt/freeldr.ini << 'INIEOF'
[FreeLoader]
DefaultOS=ReactOS
TimeOut=10

[ReactOS]
BootType=ReactOS
SystemPath=\ReactOS
Options=/DEBUG

[Display]
TitleText=ReactOS ARM64 Bootloader Test
StatusBarColor=Cyan
StatusBarTextColor=Black
BackdropTextColor=White
BackdropColor=Blue
BackdropFillStyle=Medium
TitleBoxTextColor=White
TitleBoxColor=Red
MessageBoxTextColor=White
MessageBoxColor=Blue
MenuTextColor=White
MenuColor=Blue
TextColor=Yellow
SelectedTextColor=Black
SelectedColor=Gray
ProgressBarBackgroundColor=Yellow
ProgressBarForegroundColor=Blue
INIEOF

sudo umount $TESTDIR/mnt

echo "UEFI boot image created at: $TESTDIR/uefi.img"
echo ""
echo "To test with QEMU, run:"
echo "qemu-system-aarch64 -M virt -cpu cortex-a72 -m 2048 -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd -drive file=$TESTDIR/uefi.img,format=raw,if=virtio -nographic"
