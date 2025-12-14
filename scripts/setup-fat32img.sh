qemu-img create -f raw fat32.img 256M

# Create an MBR partition table with one primary FAT32 partition
parted -s fat32.img mklabel msdos
parted -s fat32.img mkpart primary fat32 1MiB 100%
parted -s fat32.img set 1 lba on

# Attach a loop device with partition scanning
LOOP=$(sudo losetup --find --show --partscan fat32.img)

# Format partition 1 as FAT32
sudo mkfs.vfat -F 32 -n USBDISK ${LOOP}p1

# Detach
sudo losetup -d "$LOOP"


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
