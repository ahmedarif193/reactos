/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Host harness that formats an image with NtfsVolumeFormat so the
 *              result can be checked against ntfsprogs.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <unistd.h>

#include "ntfsformat.h"

#define HOST_STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001)

namespace
{

struct HostContext
{
    int Descriptor;
};

NTSTATUS
HostWrite(void* Context, ULONGLONG Offset, ULONG Length, const void* Buffer)
{
    HostContext* Host = static_cast<HostContext*>(Context);
    const unsigned char* Bytes = static_cast<const unsigned char*>(Buffer);
    ULONG Done = 0;

    while (Done < Length)
    {
        ssize_t Written = pwrite(Host->Descriptor,
                                 Bytes + Done,
                                 Length - Done,
                                 static_cast<off_t>(Offset + Done));
        if (Written <= 0)
        {
            perror("pwrite");
            return HOST_STATUS_UNSUCCESSFUL;
        }

        Done += static_cast<ULONG>(Written);
    }

    return STATUS_SUCCESS;
}

void*
HostAllocate(void* Context, ULONG Length)
{
    (void)Context;
    return malloc(Length);
}

void
HostFree(void* Context, void* Buffer)
{
    (void)Context;
    free(Buffer);
}

BOOLEAN
HostProgress(void* Context, ULONG Percent)
{
    (void)Context;
    fprintf(stderr, "format: %u%%\n", Percent);
    return TRUE;
}

/* NT time: 100 ns ticks since 1601-01-01. */
ULONGLONG
HostNtTime(void)
{
    return (static_cast<ULONGLONG>(time(nullptr)) + 11644473600ULL) *
           10000000ULL;
}

} /* namespace */

int
main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr,
                "usage: %s <image> <size-in-mib> [cluster-size] [label]\n",
                argv[0]);
        return 2;
    }

    const char* Path = argv[1];
    unsigned long long SizeMib = strtoull(argv[2], nullptr, 10);
    unsigned long ClusterSize = (argc > 3) ? strtoul(argv[3], nullptr, 10) : 0;
    const char* Label = (argc > 4) ? argv[4] : "HOSTTEST";

    const ULONG BytesPerSector = 512;
    ULONGLONG SizeBytes = SizeMib * 1024ULL * 1024ULL;

    HostContext Host;
    Host.Descriptor = open(Path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (Host.Descriptor < 0)
    {
        perror("open");
        return 1;
    }

    if (ftruncate(Host.Descriptor, static_cast<off_t>(SizeBytes)) != 0)
    {
        perror("ftruncate");
        close(Host.Descriptor);
        return 1;
    }

    /* Widen the ASCII label to UTF-16 for the formatter. */
    WCHAR LabelBuffer[33];
    size_t LabelLength = strlen(Label);
    if (LabelLength > 32)
        LabelLength = 32;
    for (size_t Index = 0; Index < LabelLength; Index++)
        LabelBuffer[Index] = static_cast<WCHAR>(Label[Index]);
    LabelBuffer[LabelLength] = 0;

    NtfsFormatParameters Parameters;
    memset(&Parameters, 0, sizeof(Parameters));
    Parameters.TotalSectors = SizeBytes / BytesPerSector;
    Parameters.BytesPerSector = BytesPerSector;
    Parameters.SectorsPerCluster = ClusterSize ? ClusterSize / BytesPerSector : 0;
    Parameters.SectorsPerTrack = 63;
    Parameters.NumberOfHeads = 255;
    Parameters.MediaDescriptor = 0xF8;
    Parameters.CurrentTime = HostNtTime();
    Parameters.VolumeLabel = LabelBuffer;
    Parameters.QuickFormat = TRUE;
    Parameters.IoContext = &Host;
    Parameters.Write = HostWrite;
    Parameters.Allocate = HostAllocate;
    Parameters.Free = HostFree;
    Parameters.Progress = HostProgress;

    NTSTATUS Status = NtfsVolumeFormat(&Parameters);

    if (fsync(Host.Descriptor) != 0)
        perror("fsync");
    close(Host.Descriptor);

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr, "NtfsVolumeFormat failed: 0x%08x\n",
                static_cast<unsigned>(Status));
        return 1;
    }

    printf("formatted %s (%llu MiB)\n", Path, SizeMib);
    return 0;
}
