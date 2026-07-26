#ifndef _NTFS_LINUX_H_
#define _NTFS_LINUX_H_

#include <ntfslib_new.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opens an image or block device read-only and installs it as the backing
 * store for subsequently opened ntfslib volumes in this process.
 */
NTSTATUS
NtfsDiskInitializeLinux(
    _In_ const char* Path,
    _Out_opt_ PULONG BytesPerSector);

/*
 * Explicit mutation-test entry point. FUSE mounts continue to use the
 * read-only initializer above.
 */
NTSTATUS
NtfsDiskInitializeLinuxWritable(
    _In_ const char* Path,
    _Out_opt_ PULONG BytesPerSector);

void
NtfsDiskCloseLinux(void);

#ifdef __cplusplus
}
#endif

#endif /* _NTFS_LINUX_H_ */
