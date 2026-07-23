# ReactOS NTFS-3G FSD integration

The build target, driver image, and service name are `ntfs3g`, `ntfs3g.sys`,
and `Ntfs3g`. The filesystem control device remains `\\Ntfs`, which is the NT
filesystem registration name and is independent of the service name.

The driver links the GPL-2.0-or-later NTFS-3G core imported under
`sdk/lib/3rdparty/ntfs3g/upstream`. The existing MIT-licensed ReactOS FSD code
is license-compatible, but the linked driver as distributed is governed by the
GPL terms of NTFS-3G.

`sdk/lib/3rdparty/ntfs3g` builds the upstream sources and shared ReactOS glue
once as the freestanding `ntfs3g_core` static library. User mode,
`ntfs3g.sys`, and FreeLdr all link that target. Their host files contain only
environment-specific allocation, locking, logging, time, and positional-I/O
callbacks; filesystem, device, mount, path, and file logic is not duplicated.

The kernel callbacks are private to this driver under `km`. FreeLdr's `ntfs`
filesystem backend uses the same SDK core for read-only mount, path lookup,
open, read, and seek operations.

The kernel FSD maps ReactOS IRPs to the same core mount, UTF-16 lookup,
metadata, directory-enumeration, and positional-read interfaces. It does not
link another NTFS parser or raw-device bridge. Mutating IRPs are rejected
because this initial integration is intentionally read-only.
