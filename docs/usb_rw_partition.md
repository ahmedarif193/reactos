# ReactOS USB Writable Partition Helper

ReactOS ISO images are produced with ISO9660 semantics. When such an image is
written directly to a USB stick the storage keeps behaving like a CD-ROM: it
is readable but cannot be modified at runtime. To keep the fast ISO build
pipeline while enabling writable space on USB media, use the helper introduced
here.

## Host-side preparation

```
sudo boot/tools/add_usb_rw_partition.sh \
    --iso /path/to/livecd.iso \
    --device /dev/sdX
```

The script flashes the ISO, rewrites the hybrid partition table, and appends a
64 MiB FAT partition labelled `REACTOS_RW`. You can change the writable size or
label with `--rw-size` and `--label`. To reuse an already-flashed stick, add
`--skip-iso`.

Requirements:

- root/administrator privileges for raw device access
- `sgdisk`, `mkfs.fat`, `partprobe`, and optionally `dd`

## Build integration

The build produces a helper bundle via:

```
ninja liveusb
```

This copies the ISO as `liveusb.iso` into the build root (e.g. `output-.../`)
and generates a pre-formatted 64 MiB FAT image (`liveusb_rw.fat`) labelled
`REACTOS_RW`. The FAT image can be
written onto the writable partition created by the helper script or mounted for
offline inspection.

## Bootloader support

The UEFI loader now identifies hybrid USB media that expose a real partition
table and treats them like standard disks. This avoids falling back to the
read-only ISO code path when a writable partition is present. Systems that
boot the ISO from plain optical media continue to work unchanged.
