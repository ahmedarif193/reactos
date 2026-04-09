## ntfslx

`ntfslx` is a staged NT-compatible port shell for the Linux NTFS driver core.

What is implemented:

- ReactOS filesystem-driver entry and mount path.
- Synchronous block-device reads and device-ioctl helpers.
- NTFS boot-sector parsing with NT-style volume probing.
- Linux-derived MST fixups for primary MFT record validation.
- Linux-derived default upcase-table generation.
- Linux-derived Unicode name comparison helpers.

What is intentionally not wired yet:

- ReactOS `fs_rec` still targets the legacy `ntfs` service.
- Full create/open/read/write/query directory semantics.
- Attribute decoding, runlist management, cache manager integration, and writeback.
- Volume-label extraction, index traversal, and file-name resolution.

Imported algorithm sources adapted into this module:

- `linux-ntfs/mst.c`
- `linux-ntfs/upcase.c`
- `linux-ntfs/unistr.c`
- selected on-disk structures from `linux-ntfs/layout.h`

The next safe step is to port non-VFS NTFS metadata walkers and attribute/runlist code,
then switch file opens and directory enumeration over to the imported core. Only after
that should `fs_rec` or the active NTFS service be redirected to `ntfslx`.
