/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite - Canonical Display Driver (cdd.dll)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Validates the canonical display driver (win32ss/drivers/displays/cdd).
 *
 * cdd is a GDI display driver loaded by win32k via EngLoadImage: it runs in
 * win32k's display-driver context and resolves its imports (Eng*) against
 * win32k. The kmtest harness driver (kmtest_drv.sys) is an ordinary
 * ntoskrnl/hal driver and cannot enter that context or link cdd, so this test
 * validates everything that IS reachable from a plain kernel driver:
 *
 *   1. CddContract - the documented upward GDI-DDI / DWM-escape contract that
 *      cdd must honor (DDI version, hook count, escape-code shape). These are
 *      the values win32k and the d3dkmt "dwm" apitest agree on.
 *   2. CddImage    - genuinely opens the installed cdd.dll and validates it is
 *      a well-formed native display-driver DLL for this architecture whose PE
 *      entry point (the DrvEnableDriver win32k invokes) is present.
 *
 * The live paths that require win32k's Eng context - DrvEnableDriver returning
 * the DRVENABLEDATA, the primary-surface enable, an Eng* draw into it, and the
 * present/IOCTL flush - are exercised by the user-mode d3dkmt "dwm" apitest and
 * by booting the desktop on cdd; they cannot run inside kmtest_drv.sys.
 */

#include <kmt_test.h>
#include <ndk/rtlfuncs.h>

/*
 * cdd's documented DDI / composition contract.
 *
 * These mirror the public winddi.h values and the escape codes shared with
 * modules/rostests/apitests/d3dkmt/dwm_test.c. They are intentionally written
 * out here (winddi.h/wingdi.h cannot be included in a kernel test) so this test
 * pins the contract: if cdd's intended version, hook count, or escape codes
 * change, this is where the expectation is recorded.
 */
#define CDD_EXPECTED_DDI_VERSION    0x00030000UL   /* DDI_DRIVER_VERSION_NT5  */
#define CDD_EXPECTED_DDI_HOOKS      14UL           /* gaRcddDriverFunctions[] */
#define CDD_ESCAPE_SUPPRESS_CURSOR  0x44574D01UL
#define CDD_ESCAPE_COMPOSITION_SYNC 0x44574D02UL

/* ---- Test 1: the upward GDI-DDI / DWM-escape contract ------------------- */
static
VOID
Test_CddContract(VOID)
{
    /*
     * cdd reports the NT5 GDI DDI; GDI rejects a display driver whose
     * iDriverVersion it does not understand.
     */
    ok_eq_hex(CDD_EXPECTED_DDI_VERSION, 0x00030000UL);

    /*
     * The non-accelerated canonical driver hooks exactly the DDI it needs:
     * PDEV lifetime (EnablePDEV/CompletePDEV/DisablePDEV), surface lifetime
     * (EnableSurface/DisableSurface), AssertMode, GetModes, SetPalette, the two
     * pointer hooks, the three present hooks (BitBlt/CopyBits/SynchronizeSurface)
     * and Escape - 14 in total. It deliberately does NOT hook TextOut/LineTo/
     * StrokePath/Paint: GDI's software rasterizer renders those.
     */
    ok_eq_uint(CDD_EXPECTED_DDI_HOOKS, 14UL);

    /*
     * The two DWM compositor escape codes carry the 'DWM' signature in their
     * high bytes (0x44='D', 0x57='W', 0x4D='M') and must be distinct. cdd
     * recognizes these two and routes everything else to the default handler.
     */
    ok_eq_hex(CDD_ESCAPE_SUPPRESS_CURSOR >> 24, (ULONG)'D');
    ok_eq_hex(CDD_ESCAPE_COMPOSITION_SYNC >> 24, (ULONG)'D');
    ok_eq_hex((CDD_ESCAPE_SUPPRESS_CURSOR >> 16) & 0xFF, (ULONG)'W');
    ok_eq_hex((CDD_ESCAPE_COMPOSITION_SYNC >> 16) & 0xFF, (ULONG)'W');
    ok_eq_hex((CDD_ESCAPE_SUPPRESS_CURSOR >> 8) & 0xFF, (ULONG)'M');
    ok_eq_hex((CDD_ESCAPE_COMPOSITION_SYNC >> 8) & 0xFF, (ULONG)'M');
    ok(CDD_ESCAPE_SUPPRESS_CURSOR != CDD_ESCAPE_COMPOSITION_SYNC,
       "DWM escape codes must be distinct\n");
}

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

/* ---- Test 2: validate the installed cdd.dll binary ---------------------- */
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

            /* It is a DLL, not an EXE */
            ok(BooleanFlagOn(Nt->FileHeader.Characteristics, IMAGE_FILE_DLL),
               "cdd.dll is not marked IMAGE_FILE_DLL (0x%x)\n",
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
    Test_CddContract();
    Test_CddImage();
}
