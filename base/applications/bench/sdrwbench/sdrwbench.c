/*
 * PROJECT:     ReactOS microSD R/W benchmark
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Truthful sequential + random I/O throughput on a real drive,
 *              cache-bypassing (FILE_FLAG_NO_BUFFERING|WRITE_THROUGH).
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR              512U
#define SEQ_CHUNK           (1U * 1024U * 1024U)
#define SEQ_CHUNKS          64U
#define SEQ_BYTES           ((ULONGLONG)SEQ_CHUNK * SEQ_CHUNKS)
#define RAND_BLOCK          4096U
#define RAND_OPS            2000U

static void emit(const char *Format, ...)
{
    char Buffer[512];
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 1] = 0;
    fputs(Buffer, stdout);
    fflush(stdout);
    OutputDebugStringA(Buffer);
}

static ULONGLONG QpcHz(void)
{
    LARGE_INTEGER F;
    QueryPerformanceFrequency(&F);
    return (ULONGLONG)F.QuadPart;
}

static ULONGLONG Now(void)
{
    LARGE_INTEGER T;
    QueryPerformanceCounter(&T);
    return (ULONGLONG)T.QuadPart;
}

static ULONGLONG BytesPerSec(ULONGLONG Bytes, ULONGLONG Ticks, ULONGLONG Hz)
{
    if (Ticks == 0)
        return 0;
    return (Bytes * Hz) / Ticks;
}

int main(int argc, char **argv)
{
    char Path[MAX_PATH];
    const char *Drive = (argc > 1) ? argv[1] : "D:";
    HANDLE File;
    PUCHAR Buffer;
    ULONGLONG Hz = QpcHz();
    ULONGLONG T0, T1;
    DWORD Done;
    ULONG i;
    ULONGLONG WrBps, RdBps;
    ULONG ReadIops, WriteIops;
    ULONG RandRange;
    ULONG Lcg = 0x1234567U;
    BOOL Ok;

    emit("SDRWBENCH_BEGIN drive=%s seq=%luMB rand=%lux%luB\n", Drive, (ULONG)(SEQ_BYTES / (1024 * 1024)), RAND_OPS, RAND_BLOCK);

    _snprintf(Path, sizeof(Path) - 1, "%s\\sdbench.tmp", Drive);
    Path[sizeof(Path) - 1] = 0;

    Buffer = (PUCHAR)VirtualAlloc(NULL, SEQ_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Buffer == NULL)
    {
        emit("SDRWBENCH_ERROR VirtualAlloc failed %lu\n", GetLastError());
        return 1;
    }
    for (i = 0; i < SEQ_CHUNK; i++)
        Buffer[i] = (UCHAR)(i * 7 + 13);

    File = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        emit("SDRWBENCH_ERROR open %s failed %lu\n", Path, GetLastError());
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return 1;
    }

    T0 = Now();
    for (i = 0; i < SEQ_CHUNKS; i++)
    {
        Ok = WriteFile(File, Buffer, SEQ_CHUNK, &Done, NULL);
        if (!Ok || Done != SEQ_CHUNK)
        {
            emit("SDRWBENCH_ERROR write chunk %lu failed %lu (%lu)\n", i, GetLastError(), Done);
            goto cleanup;
        }
    }
    FlushFileBuffers(File);
    T1 = Now();
    WrBps = BytesPerSec(SEQ_BYTES, T1 - T0, Hz);
    emit("SDRWBENCH_SEQ_WRITE %I64u.%02I64u MB/s (%I64u bytes)\n", WrBps / 1000000ULL, (WrBps % 1000000ULL) / 10000ULL, SEQ_BYTES);

    if (SetFilePointer(File, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && ReadFile(File, Buffer, SEQ_CHUNK, &Done, NULL) && Done == SEQ_CHUNK)
    {
        ULONG Bad = 0;
        for (i = 0; i < SEQ_CHUNK; i++)
            if (Buffer[i] != (UCHAR)(i * 7 + 13))
                Bad++;
        emit("SDRWBENCH_VERIFY first1MB mismatches=%lu (0=ok)\n", Bad);
    }

    if (SetFilePointer(File, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    {
        emit("SDRWBENCH_ERROR seek0 failed %lu\n", GetLastError());
        goto cleanup;
    }

    T0 = Now();
    for (i = 0; i < SEQ_CHUNKS; i++)
    {
        Ok = ReadFile(File, Buffer, SEQ_CHUNK, &Done, NULL);
        if (!Ok || Done != SEQ_CHUNK)
        {
            emit("SDRWBENCH_ERROR read chunk %lu failed %lu (%lu)\n", i, GetLastError(), Done);
            goto cleanup;
        }
    }
    T1 = Now();
    RdBps = BytesPerSec(SEQ_BYTES, T1 - T0, Hz);
    emit("SDRWBENCH_SEQ_READ %I64u.%02I64u MB/s (%I64u bytes)\n", RdBps / 1000000ULL, (RdBps % 1000000ULL) / 10000ULL, SEQ_BYTES);

    RandRange = (ULONG)(SEQ_BYTES / RAND_BLOCK);

    T0 = Now();
    for (i = 0; i < RAND_OPS; i++)
    {
        LONG Off;
        Lcg = Lcg * 1103515245U + 12345U;
        Off = (LONG)((Lcg % RandRange) * RAND_BLOCK);
        SetFilePointer(File, Off, NULL, FILE_BEGIN);
        Ok = ReadFile(File, Buffer, RAND_BLOCK, &Done, NULL);
        if (!Ok || Done != RAND_BLOCK)
        {
            emit("SDRWBENCH_ERROR rand read %lu failed %lu\n", i, GetLastError());
            goto cleanup;
        }
    }
    T1 = Now();
    ReadIops = (ULONG)((T1 > T0) ? ((ULONGLONG)RAND_OPS * Hz) / (T1 - T0) : 0);
    emit("SDRWBENCH_RAND_READ_IOPS %lu (4K)\n", ReadIops);

    T0 = Now();
    for (i = 0; i < RAND_OPS; i++)
    {
        LONG Off;
        Lcg = Lcg * 1103515245U + 12345U;
        Off = (LONG)((Lcg % RandRange) * RAND_BLOCK);
        SetFilePointer(File, Off, NULL, FILE_BEGIN);
        Ok = WriteFile(File, Buffer, RAND_BLOCK, &Done, NULL);
        if (!Ok || Done != RAND_BLOCK)
        {
            emit("SDRWBENCH_ERROR rand write %lu failed %lu\n", i, GetLastError());
            goto cleanup;
        }
    }
    FlushFileBuffers(File);
    T1 = Now();
    WriteIops = (ULONG)((T1 > T0) ? ((ULONGLONG)RAND_OPS * Hz) / (T1 - T0) : 0);
    emit("SDRWBENCH_RAND_WRITE_IOPS %lu (4K)\n", WriteIops);

cleanup:
    CloseHandle(File);
    DeleteFileA(Path);
    VirtualFree(Buffer, 0, MEM_RELEASE);
    emit("SDRWBENCH_END\n");
    return 0;
}
