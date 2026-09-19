/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */
#include <ntifs.h>
#include <ndk/halfuncs.h>

static ULONG Checks, Failures;
#define CHECK(e) do { ++Checks; if (!(e)) { ++Failures; \
    DbgPrint("RISCVKTEST: FAIL line=%lu\n", (ULONG)__LINE__); } } while (0)

static VOID NTAPI Unload(PDRIVER_OBJECT Driver)
{
    IoDeleteDevice(Driver->DeviceObject);
}

static VOID FloatingPointTest(VOID)
{
    KFLOATING_SAVE Original, Saved;
    ULONG64 Pattern = 0x400921fb54442d18ULL, Result;
    ULONG Fcsr;
    NTSTATUS Status = KeSaveFloatingPointState(&Original);
    CHECK(NT_SUCCESS(Status));
    if (!NT_SUCCESS(Status)) return;
    __asm__ __volatile__("fmv.d.x f0, %0\n\tli t0, 32\n\tfscsr t0"
                         :: "r"(Pattern) : "f0", "t0", "memory");
    Status = KeSaveFloatingPointState(&Saved);
    CHECK(NT_SUCCESS(Status));
    CHECK(Saved.F[0] == Pattern && Saved.Fcsr == 32);
    __asm__ __volatile__("fmv.d.x f0, zero\n\tfscsr zero" ::: "f0", "memory");
    CHECK(NT_SUCCESS(KeRestoreFloatingPointState(&Saved)));
    __asm__ __volatile__("fmv.x.d %0, f0\n\tfrcsr %1" : "=r"(Result), "=r"(Fcsr));
    CHECK(Result == Pattern && Fcsr == 32);
    CHECK(KeRestoreFloatingPointState(&Saved) == STATUS_INVALID_DEVICE_STATE);
    CHECK(NT_SUCCESS(KeRestoreFloatingPointState(&Original)));
}

static VOID MdlTest(VOID)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    PMDL Mdl;
    volatile ULONG *Kernel = NULL, *User = NULL;
    Low.QuadPart = Skip.QuadPart = 0;
    High.QuadPart = MAXLONGLONG;
    Mdl = MmAllocatePagesForMdl(Low, High, Skip, PAGE_SIZE);
    CHECK(Mdl != NULL);
    if (!Mdl) return;
    Kernel = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    CHECK(Kernel != NULL);
    if (Kernel)
    {
        *Kernel = 0x12345678;
        CHECK(NT_SUCCESS(MmProtectMdlSystemAddress(Mdl, PAGE_READONLY)));
        /* A kernel write through this read-only mapping is a deliberate
         * bugcheck, not a recoverable SEH probe. Verify retained contents. */
        CHECK(*Kernel == 0x12345678);
        CHECK(NT_SUCCESS(MmProtectMdlSystemAddress(Mdl, PAGE_READWRITE)));
        CHECK(*Kernel == 0x12345678);
    }
    __try
    {
        User = MmMapLockedPagesSpecifyCache(Mdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
        CHECK(User != NULL);
        if (User && Kernel)
        {
            CHECK(*User == *Kernel);
            *User = 0x87654321;
            CHECK(*Kernel == 0x87654321);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { CHECK(FALSE); }
    if (User) MmUnmapLockedPages((PVOID)User, Mdl);
    if (Kernel) MmUnmapLockedPages((PVOID)Kernel, Mdl);
    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT Driver, PUNICODE_STRING RegistryPath)
{
    volatile BOOLEAN Caught = FALSE;
    PDEVICE_OBJECT Device;
    UNREFERENCED_PARAMETER(RegistryPath);
    Driver->DriverUnload = Unload;
    DbgPrint("RISCVKTEST: BEGIN\n");
    __try { *(volatile ULONG *)(ULONG_PTR)0x42 = 1; }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Caught = TRUE;
        CHECK(GetExceptionCode() == STATUS_ACCESS_VIOLATION);
    }
    CHECK(Caught);
    FloatingPointTest();
    MdlTest();
    {
        TIME_FIELDS Time, After, Invalid = {0};
        BOOLEAN Available = HalQueryRealTimeClock(&Time);
        CHECK(Available);
        if (Available)
        {
            CHECK(Time.Year >= 2026 && Time.Month >= 1 && Time.Month <= 12);
            CHECK(!HalSetRealTimeClock(&Invalid));
            CHECK(HalSetRealTimeClock(&Time));
            CHECK(HalQueryRealTimeClock(&After));
            CHECK(Time.Year == After.Year && Time.Month == After.Month && Time.Day == After.Day);
        }
    }
    /* A legacy driver's device keeps it eligible for normal SCM unload. */
    CHECK(NT_SUCCESS(IoCreateDevice(Driver, 0, NULL, FILE_DEVICE_UNKNOWN,
                                    0, FALSE, &Device)));
    if (Driver->DeviceObject)
    {
        if (Failures) IoDeleteDevice(Driver->DeviceObject);
        else Driver->DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    }
    DbgPrint("RISCVKTEST: END checks=%lu failures=%lu\n", Checks, Failures);
    return Failures ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}
