/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     User-mode functional tests for ntfslx on D:\
 *
 * Self-contained -- no image prepopulation needed.
 * Tests volume info, free space, directory listing,
 * and file create/write/read cycle (when supported).
 */

#include <kmt_test.h>
#include <windows.h>

static void TestVolumeInfo(void)
{
    WCHAR VolumeName[64];
    WCHAR FsName[32];
    DWORD Serial, MaxComp, Flags;
    BOOL Ok;

    Ok = GetVolumeInformationW(L"D:\\",
                               VolumeName, _countof(VolumeName),
                               &Serial, &MaxComp, &Flags,
                               FsName, _countof(FsName));
    if (!Ok)
    {
        skip(TRUE, "GetVolumeInformationW failed (error %lu) -- D: not mounted by ntfslx?\n",
             GetLastError());
        return;
    }

    ok(wcscmp(FsName, L"NTFS") == 0,
       "Expected fs name 'NTFS', got '%ls'\n", FsName);
    ok(MaxComp == 255, "Expected MaxComp 255, got %lu\n", MaxComp);
    ok(wcslen(VolumeName) > 0, "Volume label should not be empty\n");
    trace("Label='%ls' Serial=0x%08lX Flags=0x%08lX\n", VolumeName, Serial, Flags);
}

static void TestFreeSpace(void)
{
    ULARGE_INTEGER FreeBytesAvailable, TotalBytes, TotalFreeBytes;
    BOOL Ok;

    Ok = GetDiskFreeSpaceExW(L"D:\\",
                             &FreeBytesAvailable,
                             &TotalBytes,
                             &TotalFreeBytes);
    if (!Ok)
    {
        skip(TRUE, "GetDiskFreeSpaceExW failed (error %lu)\n", GetLastError());
        return;
    }

    ok(TotalBytes.QuadPart > 0, "TotalBytes should be > 0\n");
    ok(TotalFreeBytes.QuadPart > 0, "TotalFreeBytes should be > 0 (got %I64u)\n",
       TotalFreeBytes.QuadPart);
    ok(TotalFreeBytes.QuadPart <= TotalBytes.QuadPart,
       "Free (%I64u) should be <= Total (%I64u)\n",
       TotalFreeBytes.QuadPart, TotalBytes.QuadPart);
    trace("Total: %I64u MB, Free: %I64u MB\n",
          TotalBytes.QuadPart / (1024*1024),
          TotalFreeBytes.QuadPart / (1024*1024));
}

static void TestDirListing(void)
{
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    DWORD Count = 0;

    hFind = FindFirstFileW(L"D:\\*", &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        skip(TRUE, "FindFirstFile D:\\* failed (error %lu)\n", GetLastError());
        return;
    }

    do
    {
        Count++;
        trace("  D:\\%ls  size=%I64u attrs=0x%lX\n",
              FindData.cFileName,
              ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow,
              FindData.dwFileAttributes);
    }
    while (FindNextFileW(hFind, &FindData));

    FindClose(hFind);

    /* A fresh mkfs.ntfs image has system metadata dirs visible */
    ok(Count >= 1, "Expected at least 1 root entry, got %lu\n", Count);
}

static void TestCreateWriteRead(void)
{
    HANDLE hFile;
    DWORD BytesWritten, BytesRead;
    BOOL Ok;
    BYTE WritePattern[4096];
    BYTE ReadBuffer[4096];
    DWORD i;
    DWORD Errors;
    LARGE_INTEGER FilePos;

    /* Fill write pattern: repeating 0x00..0xFF */
    for (i = 0; i < sizeof(WritePattern); i++)
        WritePattern[i] = (BYTE)(i % 256);

    /* Step 1: Create a new file */
    hFile = CreateFileW(L"D:\\ntfslx_rw_test.bin",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        trace("CreateFile(CREATE_ALWAYS) failed with error %lu -- file creation not yet supported\n",
              GetLastError());
        skip(TRUE, "File creation not supported, skipping R/W tests\n");
        return;
    }

    /* Step 2: Write 4KB pattern */
    Ok = WriteFile(hFile, WritePattern, sizeof(WritePattern), &BytesWritten, NULL);
    ok(Ok, "WriteFile failed with error %lu\n", GetLastError());
    ok(BytesWritten == sizeof(WritePattern),
       "Expected %u bytes written, got %lu\n", (unsigned)sizeof(WritePattern), BytesWritten);

    /* Step 3: Seek back to start */
    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx failed with error %lu\n", GetLastError());

    /* Step 4: Read back and compare */
    memset(ReadBuffer, 0xCC, sizeof(ReadBuffer));
    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok, "ReadFile failed with error %lu\n", GetLastError());
    ok(BytesRead == sizeof(WritePattern),
       "Expected %u bytes read, got %lu\n", (unsigned)sizeof(WritePattern), BytesRead);

    Errors = 0;
    for (i = 0; i < BytesRead; i++)
    {
        if (ReadBuffer[i] != WritePattern[i])
        {
            Errors++;
            if (Errors <= 3)
            {
                ok(0, "Mismatch at offset %lu: expected 0x%02X, got 0x%02X\n",
                   i, (unsigned)WritePattern[i], (unsigned)ReadBuffer[i]);
            }
        }
    }
    ok(Errors == 0, "Total read-back mismatches: %lu / %lu\n", Errors, BytesRead);

    CloseHandle(hFile);

    /* Step 5: Reopen and verify content persisted */
    hFile = CreateFileW(L"D:\\ntfslx_rw_test.bin",
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ok(0, "Reopen failed with error %lu\n", GetLastError());
        return;
    }

    memset(ReadBuffer, 0xDD, sizeof(ReadBuffer));
    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok, "Reopen ReadFile failed with error %lu\n", GetLastError());
    ok(BytesRead == sizeof(WritePattern),
       "Reopen expected %u bytes, got %lu\n", (unsigned)sizeof(WritePattern), BytesRead);
    ok(memcmp(ReadBuffer, WritePattern, sizeof(WritePattern)) == 0,
       "Reopen content mismatch\n");

    CloseHandle(hFile);

    /* Step 6: Write bigger (16KB) to same file */
    hFile = CreateFileW(L"D:\\ntfslx_rw_test.bin",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        BYTE *BigBuf;
        DWORD BigSize = 16384;

        BigBuf = HeapAlloc(GetProcessHeap(), 0, BigSize);
        if (BigBuf != NULL)
        {
            for (i = 0; i < BigSize; i++)
                BigBuf[i] = (BYTE)((i * 7 + 13) % 256);

            Ok = WriteFile(hFile, BigBuf, BigSize, &BytesWritten, NULL);
            ok(Ok, "BigWrite failed with error %lu\n", GetLastError());
            ok(BytesWritten == BigSize,
               "BigWrite expected %lu bytes, got %lu\n", BigSize, BytesWritten);

            /* Seek and re-read */
            FilePos.QuadPart = 0;
            SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);

            {
                BYTE *ReadBig = HeapAlloc(GetProcessHeap(), 0, BigSize);
                if (ReadBig != NULL)
                {
                    Ok = ReadFile(hFile, ReadBig, BigSize, &BytesRead, NULL);
                    ok(Ok, "BigRead failed with error %lu\n", GetLastError());
                    ok(BytesRead == BigSize, "BigRead got %lu\n", BytesRead);
                    ok(memcmp(ReadBig, BigBuf, BigSize) == 0,
                       "BigRead content mismatch\n");
                    HeapFree(GetProcessHeap(), 0, ReadBig);
                }
            }

            HeapFree(GetProcessHeap(), 0, BigBuf);
        }
        CloseHandle(hFile);
    }

    /* Cleanup */
    DeleteFileW(L"D:\\ntfslx_rw_test.bin");
}

static void TestSectionMapping(void)
{
    HANDLE hFile, hMapping;
    PVOID View;
    DWORD BytesWritten;
    BOOL Ok;
    BYTE TestData[256];
    DWORD i;

    for (i = 0; i < sizeof(TestData); i++)
        TestData[i] = (BYTE)(i ^ 0xAA);

    /* Create file for section test */
    hFile = CreateFileW(L"D:\\ntfslx_section.bin",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        skip(TRUE, "Section test: file creation not supported (error %lu)\n",
             GetLastError());
        return;
    }

    Ok = WriteFile(hFile, TestData, sizeof(TestData), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(TestData), "Section write failed\n");

    /* Create read-only section */
    hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY,
                                  0, sizeof(TestData), NULL);
    if (hMapping == NULL)
    {
        trace("CreateFileMapping failed with error %lu -- sections not yet supported\n",
              GetLastError());
        skip(TRUE, "Section creation not supported\n");
        CloseHandle(hFile);
        DeleteFileW(L"D:\\ntfslx_section.bin");
        return;
    }

    View = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, sizeof(TestData));
    ok(View != NULL, "MapViewOfFile failed with error %lu\n", GetLastError());
    if (View != NULL)
    {
        ok(memcmp(View, TestData, sizeof(TestData)) == 0,
           "Section content mismatch\n");
        UnmapViewOfFile(View);
    }

    CloseHandle(hMapping);
    CloseHandle(hFile);
    DeleteFileW(L"D:\\ntfslx_section.bin");
}

static void RunKernelPhases(void)
{
    static const char * const Phases[] = {
        "NtfslxFuncSmoke",
        "NtfslxFuncStressA",
        "NtfslxFuncStressB",
        "NtfslxFuncStressC",
    };
    DWORD Error;
    UINT i;

    for (i = 0; i < _countof(Phases); ++i)
    {
        trace("NTFSLX-HARNESS: dispatching %s\n", Phases[i]);
        Error = KmtRunKernelTest(Phases[i]);
        ok(Error == ERROR_SUCCESS,
           "KmtRunKernelTest(%s) failed with error %lu\n",
           Phases[i], Error);
        if (Error != ERROR_SUCCESS)
        {
            trace("NTFSLX-HARNESS: aborting after %s\n", Phases[i]);
            return;
        }
        trace("NTFSLX-HARNESS: completed %s\n", Phases[i]);
    }

    trace("NTFSLX-HARNESS: completed all phases\n");
}

START_TEST(NtfslxFunc)
{
    TestVolumeInfo();
    TestFreeSpace();
    TestDirListing();
    TestCreateWriteRead();
    TestSectionMapping();
    RunKernelPhases();
}
