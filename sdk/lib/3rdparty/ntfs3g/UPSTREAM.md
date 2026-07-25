# NTFS-3G upstream provenance

The `upstream` directory contains the `libntfs-3g` sources, public headers,
and the selected `ntfsprogs` sources required by `mkntfs` from NTFS-3G
release `2026.7.7`:

- Repository: <https://github.com/tuxera/ntfs-3g>
- Tag: `2026.7.7`
- Commit: `d327833ec1d5eb1358b6f2c37139f10a3460944d`
- License: GNU GPL version 2 or, at your option, any later version

The imported files match the tagged tree except for these minimal
`libntfs-3g` correctness fixes:

- `dir.c` preserves the bitmap bit offset when directory enumeration resumes.
- `lcnalloc.c` continues scanning fragmented free ranges in the current bitmap
  block before advancing.

ReactOS build files, configuration headers, and platform adapters live outside
`upstream` so future source updates can be compared directly with the tagged
upstream tree.
