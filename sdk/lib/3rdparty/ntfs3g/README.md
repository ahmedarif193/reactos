# ReactOS NTFS-3G SDK library

`ntfs3g_core` is the single freestanding static build of the imported NTFS-3G
core. It also owns the shared read-only device, volume, UTF-8/UTF-16 path,
metadata, directory, file, runtime, and logging bridges used by every ReactOS
execution environment.

Consumers provide only the primitives that cannot be shared across execution
environments:

- allocation and release;
- serialization;
- current time and log output;
- positional reads and device-context release.

The adapters are:

- `reactos/um/host.c` for the `ntfs3g_um` SDK convenience target;
- `drivers/filesystems/ntfs3g/km/host.c` for `ntfs3g.sys`;
- `boot/freeldr/freeldr/lib/fs/ntfs.c` for BIOS and UEFI FreeLdr.

All mounts are intentionally read-only. The imported `upstream` subtree is
kept unchanged at the revision recorded in `UPSTREAM.md`.

The NTFS-3G core is GPL-2.0-or-later, including when exposed through these SDK
targets. A distributed program that statically links `ntfs3g_core` or
`ntfs3g_um` must be GPL-compatible, and its distributor must satisfy the GPL
source and license requirements. These targets are therefore for ReactOS and
other GPL-compatible consumers, not a permissive static SDK for proprietary
applications.

Fallible core entry points return zero on success and a negative POSIX error
number on failure; directory reads return one for an entry, zero at end, or a
negative error. This keeps error delivery tied to each call in kernel,
FreeLdr, and user mode instead of sharing a cross-thread last-error slot.
