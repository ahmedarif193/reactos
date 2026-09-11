/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite - Canonical Display Driver (cdd.dll)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Checks the installed native display image. Entry-point execution and display
 * operations require win32k's Eng context and are covered by userspace tests.
 */

#include <kmt_test.h>
#include <ndk/rtlfuncs.h>

/* Read the NT headers of the running kmtest driver to learn this build's CPU. */
static
USHORT
KmtSelfMachine(VOID)
{
    PIMAGE_DOS_HEADER Dos;
    PIMAGE_NT_HEADERS Nt;

    if (KmtDriverObject == NULL || KmtDriverObject->DriverStart == NULL)
        return 0;

    Dos = (PIMAGE_DOS_HEADER)KmtDriverObject->DriverStart;
    if (Dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    Nt = (PIMAGE_NT_HEADERS)((PUCHAR)Dos + Dos->e_lfanew);
    if (Nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    return Nt->FileHeader.Machine;
}

/* Validate the installed display image. */
static
VOID
Test_CddImage(VOID)
{
    static const ULONG BufferSize = PAGE_SIZE; /* headers live in page 0 */
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\system32\\cdd.dll");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    HANDLE Handle;
    NTSTATUS Status;
    PUCHAR Buffer;
    PIMAGE_DOS_HEADER Dos;
    PIMAGE_NT_HEADERS Nt;

    if (skip(KeGetCurrentIrql() == PASSIVE_LEVEL, "Not at PASSIVE_LEVEL\n"))
        return;

    InitializeObjectAttributes(&ObjectAttributes,
                               &FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateFile(&Handle,
                          GENERIC_READ | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0);
    if (skip(NT_SUCCESS(Status), "cdd.dll not installed (0x%lx); skipping image checks\n", Status))
        return;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, BufferSize, 'TddC');
    if (!ok(Buffer != NULL, "Out of memory\n"))
    {
        ZwClose(Handle);
        return;
    }

    Offset.QuadPart = 0;
    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus,
                        Buffer, BufferSize, &Offset, NULL);
    ZwClose(Handle);

    ok(NT_SUCCESS(Status), "ZwReadFile failed: 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        ok(IoStatus.Information >= sizeof(IMAGE_DOS_HEADER),
           "Short read: %lu bytes\n", (ULONG)IoStatus.Information);

        /* DOS header */
        Dos = (PIMAGE_DOS_HEADER)Buffer;
        ok_eq_hex(Dos->e_magic, IMAGE_DOS_SIGNATURE);

        /* e_lfanew must address the NT headers wholly inside our buffer */
        if (Dos->e_magic == IMAGE_DOS_SIGNATURE &&
            (ULONG)Dos->e_lfanew > 0 &&
            (ULONG)Dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) <= BufferSize)
        {
            Nt = (PIMAGE_NT_HEADERS)(Buffer + Dos->e_lfanew);

            /* PE signature */
            ok_eq_hex(Nt->Signature, IMAGE_NT_SIGNATURE);

            /* Built for the same CPU as the running kernel (arch-neutral) */
            ok_eq_hex(Nt->FileHeader.Machine, KmtSelfMachine());

            /* Windows display images need not carry IMAGE_FILE_DLL. */
            ok(BooleanFlagOn(Nt->FileHeader.Characteristics, IMAGE_FILE_EXECUTABLE_IMAGE),
               "cdd.dll is not marked IMAGE_FILE_EXECUTABLE_IMAGE (0x%x)\n",
               Nt->FileHeader.Characteristics);

            /* A 64-bit kernel image uses the PE32+ optional header */
            ok_eq_hex(Nt->OptionalHeader.Magic, IMAGE_NT_OPTIONAL_HDR_MAGIC);

            /* Display drivers are native-subsystem images */
            ok_eq_uint(Nt->OptionalHeader.Subsystem, (USHORT)IMAGE_SUBSYSTEM_NATIVE);

            /*
             * The PE entry point is RcddEnableDriver - this is exactly what
             * win32k's loader returns for the "DrvEnableDriver" lookup
             * (EngFindImageProcAddress -> pGdiDriverInfo->EntryPoint), so a
             * non-zero entry point means the GDI driver entry is present.
             */
            ok(Nt->OptionalHeader.AddressOfEntryPoint != 0,
               "cdd.dll has no entry point (DrvEnableDriver missing)\n");

            ok(Nt->OptionalHeader.SizeOfImage >= PAGE_SIZE,
               "Implausible SizeOfImage 0x%lx\n", Nt->OptionalHeader.SizeOfImage);
        }
        else
        {
            ok(FALSE, "Bad e_lfanew 0x%lx\n", (ULONG)Dos->e_lfanew);
        }
    }

    ExFreePoolWithTag(Buffer, 'TddC');
}

START_TEST(CddDisplay)
{
    Test_CddImage();
}
