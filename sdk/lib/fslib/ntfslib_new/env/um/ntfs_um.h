#ifndef _NTFS_UM_H_
#define _NTFS_UM_H_

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS
NtfsDiskInitializeUm(
    _In_      HANDLE FileHandle,
    _Out_opt_ ULONG* BytesPerSector);

#ifdef __cplusplus
}
#endif

#endif /* _NTFS_UM_H_ */
