/*
 * Native Linux environment glue for ntfslib_new.
 *
 * The first FUSE frontend is deliberately read-only. That makes image-backed
 * parser testing safe while allocation, journal replay, and sparse/compressed
 * write support are still incomplete in the shared core.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <ntfslib_new.h>
#include <ntfslib_new_internal.h>
#include "ntfs_linux.h"

static int NtfsImageFd = -1;
static BOOLEAN NtfsImageWritable = FALSE;

BOOLEAN
NtfsIsNameInExpressionFallback(
    _In_ PUNICODE_STRING Expression,
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN IgnoreCase,
    _In_opt_ PWCHAR UpcaseTable);

static NTSTATUS
NtfsDiskInitializeLinuxInternal(
    _In_ const char* Path,
    _Out_opt_ PULONG BytesPerSector,
    _In_ BOOLEAN Writable)
{
    UCHAR BootSector[512];
    ssize_t BytesRead;
    ULONG SectorSize;
    int NewFd;

    if (!Path)
        return STATUS_INVALID_PARAMETER;

    NewFd = open(Path,
                 (Writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
    if (NewFd < 0)
    {
        if (errno == EACCES || errno == EROFS)
            return Writable
                ? STATUS_MEDIA_WRITE_PROTECTED
                : STATUS_ACCESS_DENIED;
        return STATUS_NOT_FOUND;
    }

    do
    {
        BytesRead = pread(NewFd, BootSector, sizeof(BootSector), 0);
    } while (BytesRead < 0 && errno == EINTR);

    if (BytesRead != (ssize_t)sizeof(BootSector))
    {
        close(NewFd);
        return STATUS_END_OF_FILE;
    }

    SectorSize = (ULONG)BootSector[0x0b] |
                 ((ULONG)BootSector[0x0c] << 8);
    if (SectorSize != 512 && SectorSize != 4096)
    {
        close(NewFd);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (NtfsImageFd >= 0)
        close(NtfsImageFd);
    NtfsImageFd = NewFd;
    NtfsImageWritable = Writable;

    if (BytesPerSector)
        *BytesPerSector = SectorSize;
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS
NtfsDiskInitializeLinux(
    _In_ const char* Path,
    _Out_opt_ PULONG BytesPerSector)
{
    return NtfsDiskInitializeLinuxInternal(Path,
                                           BytesPerSector,
                                           FALSE);
}

extern "C" NTSTATUS
NtfsDiskInitializeLinuxWritable(
    _In_ const char* Path,
    _Out_opt_ PULONG BytesPerSector)
{
    return NtfsDiskInitializeLinuxInternal(Path,
                                           BytesPerSector,
                                           TRUE);
}

extern "C" void
NtfsDiskCloseLinux(void)
{
    if (NtfsImageFd >= 0)
    {
        close(NtfsImageFd);
        NtfsImageFd = -1;
    }
    NtfsImageWritable = FALSE;
}

extern "C" void*
NtfsAllocatePoolWithTag(POOL_TYPE PoolType, size_t Size, ULONG Tag)
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(Tag);
    return malloc(Size ? Size : 1);
}

extern "C" void
NtfsFreePool(void* Object)
{
    free(Object);
}

extern "C" NTSTATUS
NtfsQuerySystemTime(_Out_ PULONGLONG NtfsTime)
{
    static const LONGLONG UnixEpochInNtSeconds =
        INT64_C(11644473600);
    struct timespec Current;
    LONGLONG Seconds;

    if (!NtfsTime)
        return STATUS_INVALID_PARAMETER;
    if (clock_gettime(CLOCK_REALTIME, &Current) != 0)
        return STATUS_INVALID_DEVICE_STATE;
    if (Current.tv_nsec < 0 ||
        Current.tv_nsec >= INT64_C(1000000000))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Seconds = (LONGLONG)Current.tv_sec;
    if ((time_t)Seconds != Current.tv_sec ||
        Seconds < -UnixEpochInNtSeconds ||
        Seconds >
            INT64_MAX - UnixEpochInNtSeconds)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    Seconds += UnixEpochInNtSeconds;
    if ((ULONGLONG)Seconds >
            (~(ULONGLONG)0) /
                UINT64_C(10000000))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    *NtfsTime =
        (ULONGLONG)Seconds *
            UINT64_C(10000000) +
        (ULONGLONG)Current.tv_nsec / 100;
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS
NtfsReadVolume(_In_ ULONGLONG Offset,
               _In_ ULONG Length,
               _Inout_ PUCHAR Buffer)
{
    ULONG Completed = 0;

    if (NtfsImageFd < 0)
        return STATUS_INVALID_DEVICE_STATE;
    if (!Buffer && Length != 0)
        return STATUS_INVALID_PARAMETER;
    if (Offset > INT64_MAX ||
        Length > (ULONGLONG)INT64_MAX - Offset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    while (Completed < Length)
    {
        ssize_t Result = pread(NtfsImageFd,
                               Buffer + Completed,
                               Length - Completed,
                               (off_t)(Offset + Completed));
        if (Result > 0)
        {
            Completed += (ULONG)Result;
            continue;
        }
        if (Result < 0 && errno == EINTR)
            continue;
        if (Result == 0)
            return STATUS_END_OF_FILE;
        return errno == EACCES ? STATUS_ACCESS_DENIED : STATUS_INVALID_DEVICE_STATE;
    }

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS
NtfsWriteVolume(_In_ ULONGLONG Offset,
                _In_ ULONG Length,
                _Inout_ PUCHAR Buffer)
{
    ULONG Completed = 0;

    if (NtfsImageFd < 0)
        return STATUS_INVALID_DEVICE_STATE;
    if (!NtfsImageWritable)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (!Buffer && Length != 0)
        return STATUS_INVALID_PARAMETER;
    if (Offset > INT64_MAX ||
        Length > (ULONGLONG)INT64_MAX - Offset)
    {
        return STATUS_FILE_TOO_LARGE;
    }

    while (Completed < Length)
    {
        ssize_t Result = pwrite(NtfsImageFd,
                                Buffer + Completed,
                                Length - Completed,
                                (off_t)(Offset + Completed));
        if (Result > 0)
        {
            Completed += (ULONG)Result;
            continue;
        }
        if (Result < 0 && errno == EINTR)
            continue;
        if (errno == EACCES || errno == EROFS)
            return STATUS_MEDIA_WRITE_PROTECTED;
        if (errno == ENOSPC)
            return STATUS_DISK_FULL;
        if (errno == EFBIG)
            return STATUS_FILE_TOO_LARGE;
        return STATUS_INVALID_DEVICE_STATE;
    }

    return STATUS_SUCCESS;
}

extern "C" BOOLEAN
NtfsIsNameInExpression(_In_ PUNICODE_STRING Expression,
                       _In_ PUNICODE_STRING Name,
                       _In_ BOOLEAN IgnoreCase,
                       _In_opt_ PWCHAR UpcaseTable)
{
    return NtfsIsNameInExpressionFallback(Expression,
                                          Name,
                                          IgnoreCase,
                                          UpcaseTable);
}
