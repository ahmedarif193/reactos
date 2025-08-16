#!/bin/bash

# Create a test directory for the ARM64 UEFI boot
TESTDIR=/tmp/reactos-arm64-test
rm -rf $TESTDIR
mkdir -p $TESTDIR/esp/EFI/BOOT

# Copy the bootloader with the standard ARM64 UEFI name
cp /home/ahmed/WorkDir/reactos/output-MinGW-arm64/boot/freeldr/freeldr/uefildr.efi $TESTDIR/esp/EFI/BOOT/BOOTAA64.EFI

# Create a simple freeldr.ini configuration
cat > $TESTDIR/esp/freeldr.ini << 'INIEOF'
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

# Create the FAT32 image using mtools (no sudo required)
dd if=/dev/zero of=$TESTDIR/uefi.img bs=1M count=64
mformat -F -i $TESTDIR/uefi.img ::
mmd -i $TESTDIR/uefi.img ::/EFI
mmd -i $TESTDIR/uefi.img ::/EFI/BOOT
mcopy -i $TESTDIR/uefi.img $TESTDIR/esp/EFI/BOOT/BOOTAA64.EFI ::/EFI/BOOT/
mcopy -i $TESTDIR/uefi.img $TESTDIR/esp/freeldr.ini ::/

echo "UEFI boot image created at: $TESTDIR/uefi.img"
echo ""
echo "Contents of the image:"
mdir -i $TESTDIR/uefi.img ::
mdir -i $TESTDIR/uefi.img ::/EFI/BOOT/
echo ""
echo "To test with QEMU (you need qemu-efi-aarch64 package installed), run:"
echo "qemu-system-aarch64 -M virt -cpu cortex-a72 -m 2048 -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd -drive file=$TESTDIR/uefi.img,format=raw,if=virtio -nographic"
