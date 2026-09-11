/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Windows 11 dxgkrnl public D3DKMT export routing test
 */

#include <kmt_test.h>
#include <reactos/rddm/rxgkntgdi.h>

#define RXGK_STRING_INNER(Value) #Value
#define RXGK_STRING(Value) RXGK_STRING_INNER(Value)

PVOID
NTAPI
RtlImageDirectoryEntryToData(
    _In_ PVOID BaseAddress,
    _In_ BOOLEAN MappedAsImage,
    _In_ USHORT Directory,
    _Out_ PULONG Size);

typedef struct _RXGK_TEST_EXPORT
{
    ULONG Ordinal;
    PCSTR Name;
} RXGK_TEST_EXPORT;

#define RXGK_TEST_EXPORT_ENTRY(Ordinal, Name) \
    { Ordinal, RXGK_STRING(NtGdiDdDDI##Name) },

static const RXGK_TEST_EXPORT RxgkPublicExports[] =
{
    RXGK_NTGDI_EXPORTS_0(RXGK_TEST_EXPORT_ENTRY)
    RXGK_NTGDI_EXPORTS_1(RXGK_TEST_EXPORT_ENTRY)
    RXGK_NTGDI_EXPORTS_2(RXGK_TEST_EXPORT_ENTRY)
    RXGK_NTGDI_EXPORTS_5(RXGK_TEST_EXPORT_ENTRY)
    /*
     * These scheduler exports require private adapter or policy state. Check
     * their name/ordinal contract without passing NULL internal objects or
     * changing timeout controls used by the running display driver.
     */
    { 2, "DxgKrnlTelemetryGlobal_LogTelemetryEvent" },
    { 11, "SysMmMapIommuContiguousRange" },
    { 12, "SysMmMapIommuRange" },
    { 13, "SysMmUnmapIommuContiguousRange" },
    { 14, "SysMmUnmapIommuRange" },
    { 23, "TdrIsEnabled" },
    { 25, "TdrIsTimeoutForcedFlip" },
    { 37, "g_TdrForceTimeout" },
    { 48, "DxgkSubmitPresentBltToHwQueue" },
    { 83, "NtDxgkSubmitPresentBltToHwQueue" },
};

#undef RXGK_TEST_EXPORT_ENTRY

static PVOID
RxgkFindExport(
    _In_ PVOID ImageBase,
    _In_ PCSTR Name,
    _Out_opt_ PULONG ExportOrdinal)
{
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PULONG NameTable;
    PUSHORT OrdinalTable;
    PULONG FunctionTable;
    ULONG DirectorySize;
    ULONG Index;

    ExportDirectory = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &DirectorySize);
    if (ExportDirectory == NULL || DirectorySize < sizeof(*ExportDirectory))
        return NULL;

    NameTable = (PULONG)((PUCHAR)ImageBase + ExportDirectory->AddressOfNames);
    OrdinalTable = (PUSHORT)((PUCHAR)ImageBase + ExportDirectory->AddressOfNameOrdinals);
    FunctionTable = (PULONG)((PUCHAR)ImageBase + ExportDirectory->AddressOfFunctions);

    for (Index = 0; Index < ExportDirectory->NumberOfNames; ++Index)
    {
        ULONG FunctionIndex;
        ULONG FunctionRva;

        if (strcmp((PCSTR)((PUCHAR)ImageBase + NameTable[Index]), Name) != 0)
            continue;

        FunctionIndex = OrdinalTable[Index];
        if (FunctionIndex >= ExportDirectory->NumberOfFunctions)
            return NULL;

        FunctionRva = FunctionTable[FunctionIndex];
        if (FunctionRva == 0)
            return NULL;

        if (ExportOrdinal != NULL)
            *ExportOrdinal = ExportDirectory->Base + FunctionIndex;
        return (PUCHAR)ImageBase + FunctionRva;
    }

    return NULL;
}

START_TEST(DxgkrnlPublicExports)
{
    typedef NTSTATUS (NTAPI *PONE_ARGUMENT)(_In_opt_ PVOID Argument);
    typedef NTSTATUS (NTAPI *PTWO_ARGUMENTS)(_In_opt_ PVOID Argument0, _In_opt_ PVOID Argument1);
    typedef NTSTATUS (NTAPI *PFIVE_ARGUMENTS)(_In_ ULONG Argument0, _In_opt_ PVOID Argument1, _In_opt_ PVOID Argument2, _In_ ULONG Argument3, _In_opt_ PVOID Argument4);
    PFIVE_ARGUMENTS ShareObjects;
    PTWO_ARGUMENTS GetPriorityClass;
    PONE_ARGUMENT CloseAdapter;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    UNICODE_STRING Name;
    NTSTATUS Status;
    ULONG Index;

    RtlInitUnicodeString(&Name, L"\\Device\\DxgKrnl");
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &FileObject, &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    for (Index = 0; Index < RTL_NUMBER_OF(RxgkPublicExports); ++Index)
    {
        ULONG ExportOrdinal = 0;
        PVOID Export = RxgkFindExport(DeviceObject->DriverObject->DriverStart, RxgkPublicExports[Index].Name, &ExportOrdinal);

        ok(Export != NULL, "ordinal %lu export %s is missing\n", RxgkPublicExports[Index].Ordinal, RxgkPublicExports[Index].Name);
        if (Export != NULL)
            ok_eq_ulong(ExportOrdinal, RxgkPublicExports[Index].Ordinal);
    }

    CloseAdapter = (PONE_ARGUMENT)RxgkFindExport(DeviceObject->DriverObject->DriverStart, "NtGdiDdDDICloseAdapter", NULL);
    ok(CloseAdapter != NULL, "CloseAdapter export is missing\n");
    if (CloseAdapter != NULL)
    {
        Status = CloseAdapter(NULL);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    }

    GetPriorityClass = (PTWO_ARGUMENTS)RxgkFindExport(DeviceObject->DriverObject->DriverStart, "NtGdiDdDDIGetProcessSchedulingPriorityClass", NULL);
    ok(GetPriorityClass != NULL, "GetProcessSchedulingPriorityClass export is missing\n");
    if (GetPriorityClass != NULL)
    {
        Status = GetPriorityClass(NULL, NULL);
        ok(Status != STATUS_DEVICE_NOT_READY && Status != STATUS_PROCEDURE_NOT_FOUND, "two-argument dispatch was not registered: 0x%08lx\n", Status);
    }

    ShareObjects = (PFIVE_ARGUMENTS)RxgkFindExport(DeviceObject->DriverObject->DriverStart, "NtGdiDdDDIShareObjects", NULL);
    ok(ShareObjects != NULL, "ShareObjects export is missing\n");
    if (ShareObjects != NULL)
    {
        Status = ShareObjects(0, NULL, NULL, 0, NULL);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    }

    ObDereferenceObject(FileObject);
}
