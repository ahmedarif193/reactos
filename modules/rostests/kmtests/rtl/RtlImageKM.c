/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite RTL image helper API
 */

#include <kmt_test.h>
#include <ndk/rtlfuncs.h>

#define NDEBUG
#include <debug.h>

START_TEST(RtlImageKM)
{
    PVOID ImageBase = KmtDriverObject->DriverStart;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID Data;
    ULONG Size;

    ok(ImageBase != NULL, "DriverStart NULL\n");
    if (ImageBase == NULL) return;

    NtHeaders = RtlImageNtHeader(ImageBase);
    ok(NtHeaders != NULL, "RtlImageNtHeader failed\n");
    if (NtHeaders == NULL) return;

    ok_eq_hex(NtHeaders->Signature, 0x00004550UL);
#ifdef _M_ARM64
    ok_eq_hex(NtHeaders->FileHeader.Machine, 0xAA64);
#endif
    ok(NtHeaders->OptionalHeader.SizeOfImage >= PAGE_SIZE, "SizeOfImage %lx\n", NtHeaders->OptionalHeader.SizeOfImage);

    Size = 0;
    Data = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &Size);
    ok(Data != NULL, "no import directory\n");
    ok(Size != 0, "import directory empty\n");
    if (Data != NULL)
    {
        PIMAGE_IMPORT_DESCRIPTOR Import = Data;
        PCSTR DllName = (PCSTR)((PUCHAR)ImageBase + Import->Name);
        ok(_stricmp(DllName, "ntoskrnl.exe") == 0, "first import dll %s\n", DllName);
    }

    Data = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXCEPTION, &Size);
#ifdef _M_ARM64
    ok(Data != NULL, "no exception directory\n");
    ok(Size != 0, "exception directory empty\n");
#endif

    NtHeaders = RtlImageNtHeader((PUCHAR)ImageBase + 2);
    ok_eq_pointer(NtHeaders, NULL);
}
