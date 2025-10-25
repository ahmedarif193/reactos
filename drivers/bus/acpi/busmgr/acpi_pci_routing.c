/*
 * PROJECT:         ReactOS ACPI Bus Manager
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * PURPOSE:         Minimal ACPI _PRT provider for HAL PCI routing
 */

#include <ntddk.h>
#include <acpi.h>
#include <acpixf.h>

/* HAL hook: register PCI routing provider. Avoid including hal.h; declare here. */
typedef BOOLEAN (NTAPI *PHAL_PCI_ROUTE_QUERY)(UCHAR Bus, UCHAR Device, UCHAR Function, UCHAR Pin, PULONG GsiOut);
NTSYSAPI VOID NTAPI HalpRegisterPciRouteQuery(PHAL_PCI_ROUTE_QUERY Provider);

typedef struct _PRT_MAP_ENTRY
{
    UCHAR Bus;    /* PCI bus number (_BBN) */
    UCHAR Device; /* device number */
    UCHAR Pin;    /* 1..4 (INTA#..INTD#) */
    ULONG Gsi;    /* Global System Interrupt */
} PRT_MAP_ENTRY, *PPRT_MAP_ENTRY;

static PRT_MAP_ENTRY PrtCache[256];
static ULONG PrtCacheCount;

static ACPI_STATUS
AcpiPrtBuildForHandle(ACPI_HANDLE Handle, UCHAR RootBus)
{
    ACPI_BUFFER Buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_STATUS Status;
    PRT_MAP_ENTRY entry;

    Status = AcpiGetIrqRoutingTable(Handle, &Buffer);
    if (ACPI_FAILURE(Status)) return Status;

    ACPI_PCI_ROUTING_TABLE* p = (ACPI_PCI_ROUTING_TABLE*)Buffer.Pointer;
    while (p && p->Length)
    {
        entry.Bus = RootBus;
        entry.Device = (UCHAR)((p->Address >> 16) & 0xFF);
        entry.Pin = (UCHAR)((p->Pin & 0x3) + 1); /* 1..4 */

        if (!p->Source[0])
        {
            /* Direct GSI */
            entry.Gsi = p->SourceIndex; /* ACPICA maps GSI here when Source==NULL */
            if (PrtCacheCount < RTL_NUMBER_OF(PrtCache)) PrtCache[PrtCacheCount++] = entry;
        }
        else
        {
            /* Link device; resolve its current resources */
            ACPI_HANDLE LinkHandle;
            ACPI_STATUS st2 = AcpiGetHandle(Handle, p->Source, &LinkHandle);
            if (ACPI_SUCCESS(st2))
            {
                ACPI_BUFFER resBuf = { ACPI_ALLOCATE_BUFFER, NULL };
                st2 = AcpiGetCurrentResources(LinkHandle, &resBuf);
                if (ACPI_SUCCESS(st2) && resBuf.Pointer)
                {
                    ACPI_RESOURCE* res = (ACPI_RESOURCE*)resBuf.Pointer;
                    /* Index into the link resource that _PRT references */
                    UINT32 wantIndex = p->SourceIndex;
                    while (res && res->Type != ACPI_RESOURCE_TYPE_END_TAG)
                    {
                        if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ)
                        {
                            if (res->Data.ExtendedIrq.InterruptCount >= 1)
                            {
                                /* Choose by SourceIndex when available, else first */
                                UINT32 idx = (wantIndex < res->Data.ExtendedIrq.InterruptCount) ? wantIndex : 0;
                                entry.Gsi = (ULONG)res->Data.ExtendedIrq.Interrupts[idx];
                                if (PrtCacheCount < RTL_NUMBER_OF(PrtCache)) PrtCache[PrtCacheCount++] = entry;
                                break;
                            }
                        }
                        else if (res->Type == ACPI_RESOURCE_TYPE_IRQ)
                        {
                            if (res->Data.Irq.InterruptCount >= 1)
                            {
                                /* Choose by SourceIndex when available, else first */
                                UINT32 idx = (wantIndex < res->Data.Irq.InterruptCount) ? wantIndex : 0;
                                entry.Gsi = (ULONG)res->Data.Irq.Interrupts[idx];
                                if (PrtCacheCount < RTL_NUMBER_OF(PrtCache)) PrtCache[PrtCacheCount++] = entry;
                                break;
                            }
                        }
                        res = ACPI_NEXT_RESOURCE(res);
                    }
                    AcpiOsFree(resBuf.Pointer);
                }
            }
        }
        p = (ACPI_PCI_ROUTING_TABLE*)((PUCHAR)p + p->Length);
    }

    AcpiOsFree(Buffer.Pointer);
    return AE_OK;
}

static BOOLEAN NTAPI
HalPciRouteProvider(_In_ UCHAR Bus,
                    _In_ UCHAR Device,
                    _In_ UCHAR Function,
                    _In_ UCHAR Pin,
                    _Out_ PULONG GsiOut)
{
    UNREFERENCED_PARAMETER(Function);
    for (ULONG i = 0; i < PrtCacheCount; ++i)
    {
        if (PrtCache[i].Bus == Bus && PrtCache[i].Device == Device && PrtCache[i].Pin == Pin)
        {
            *GsiOut = PrtCache[i].Gsi;
            return TRUE;
        }
    }
    return FALSE;
}

/* Enumerate all PCI root bridges and cache their PRT */
static ACPI_STATUS
AcpiEnumRootBridgeCallback(ACPI_HANDLE ObjHandle, UINT32 NestingLevel, void* Context, void** ReturnValue)
{
    UNREFERENCED_PARAMETER(NestingLevel);
    UNREFERENCED_PARAMETER(ReturnValue);
    ACPI_DEVICE_INFO* Info;
    ACPI_STATUS Status;
    UCHAR Bus = 0;

    Status = AcpiGetObjectInfo(ObjHandle, &Info);
    if (ACPI_FAILURE(Status)) return AE_OK;

    /* Get bus number (_BBN) if available */
    ACPI_OBJECT out;
    ACPI_BUFFER outBuf = { sizeof(out), &out };
    Status = AcpiEvaluateObjectTyped(ObjHandle, (char*)"_BBN", NULL, &outBuf, ACPI_TYPE_INTEGER);
    if (ACPI_SUCCESS(Status))
    {
        Bus = (UCHAR)out.Integer.Value;
    }
    AcpiOsFree(Info);

    /* Build routing cache for this root bridge */
    AcpiPrtBuildForHandle(ObjHandle, Bus);
    return AE_OK;
}

VOID
AcpiRegisterPrtProvider(VOID)
{
    /* Enumerate PCI/PCIe root bridges by HID */
    (void)AcpiGetDevices("PNP0A03", AcpiEnumRootBridgeCallback, NULL, NULL);
    (void)AcpiGetDevices("PNP0A08", AcpiEnumRootBridgeCallback, NULL, NULL);

    /* Register provider if we cached any entries */
    if (PrtCacheCount)
    {
        HalpRegisterPciRouteQuery(HalPciRouteProvider);
    }
}
