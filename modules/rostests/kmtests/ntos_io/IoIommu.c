/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DMA IOMMU interface parity tests without a hardware provider
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(IoIommu)
{
    IOMMU_DMA_DOMAIN_CREATION_FLAGS DomainFlags = {0};
    DMA_IOMMU_INTERFACE LegacyInterface;
    DMA_IOMMU_INTERFACE_EX Interface;
    PIOMMU_DMA_DOMAIN Domain;
    PVOID *Routines;
    NTSTATUS Status;
    ULONG Index;
    ULONG Version;

    RtlFillMemory(&LegacyInterface, sizeof(LegacyInterface), 0xA5);
    Status = IoGetIommuInterface(1, &LegacyInterface);
    if (Status == STATUS_NOT_SUPPORTED)
    {
        skip(FALSE, "legacy IOMMU interface provider is unavailable\n");
    }
    else
    {
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_ulong(LegacyInterface.Version, 1);
            Routines = (PVOID *)&LegacyInterface.CreateDomain;
            for (Index = 0; Index != 13; Index++)
                ok(Routines[Index] != NULL,
                   "legacy IOMMU routine[%lu] is NULL\n", Index);
        }

        RtlFillMemory(&LegacyInterface, sizeof(LegacyInterface), 0xA5);
        Status = IoGetIommuInterface(0, &LegacyInterface);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER_1);
        ok_eq_ulong(LegacyInterface.Version, 0xA5A5A5A5);
    }

    RtlFillMemory(&Interface, sizeof(Interface), 0xA5);
    Status = IoGetIommuInterfaceEx(1, 0, &Interface);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_size(Interface.Size, 0x78);
        ok_eq_ulong(Interface.Version, 1);
        Routines = (PVOID *)&Interface.V1;
        for (Index = 0; Index != 13; Index++)
            ok(Routines[Index] != NULL,
               "IOMMU routine[%lu] is NULL\n", Index);

        Domain = (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5;
        Status = Interface.V1.CreateDomain(TRUE, &Domain);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok_eq_pointer(Domain,
                      (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5);

        Domain = (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5;
        Status = Interface.V1.CreateDomain(FALSE, &Domain);
        ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
        ok_eq_pointer(Domain,
                      (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5);
    }

    for (Version = 0; Version <= 4; Version++)
    {
        if (Version == 1)
            continue;

        RtlFillMemory(&Interface, sizeof(Interface), 0xA5);
        Status = IoGetIommuInterfaceEx(Version, 0, &Interface);
        if ((Version == 0) || (Version == 4))
        {
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER_1);
            ok_eq_size(Interface.Size,
                       (SIZE_T)0xA5A5A5A5A5A5A5A5ULL);
            continue;
        }

        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_size(Interface.Size, Version == 2 ? 0xC0 : 0xE8);
        ok_eq_ulong(Interface.Version, Version);

        Routines = Version == 2 ? (PVOID *)&Interface.V2 :
                                  (PVOID *)&Interface.V3;
        for (Index = 0; Index != (Version == 2 ? 22 : 27); Index++)
            ok(Routines[Index] != NULL,
               "IOMMU V%lu routine[%lu] is NULL\n", Version, Index);

        Domain = (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5;
        if (Version == 2)
        {
            Status = Interface.V2.CreateDomainEx(DomainTypeTranslate,
                                                 DomainFlags,
                                                 NULL,
                                                 NULL,
                                                 &Domain);
        }
        else
        {
            Status = Interface.V3.CreateDomainEx(DomainTypeTranslate,
                                                 DomainFlags,
                                                 NULL,
                                                 NULL,
                                                 &Domain);
        }
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        ok_eq_pointer(Domain,
                      (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5);

        Domain = (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5;
        if (Version == 2)
        {
            Status = Interface.V2.CreateDomainEx(DomainTypeUnmanaged,
                                                 DomainFlags,
                                                 NULL,
                                                 NULL,
                                                 &Domain);
        }
        else
        {
            Status = Interface.V3.CreateDomainEx(DomainTypeUnmanaged,
                                                 DomainFlags,
                                                 NULL,
                                                 NULL,
                                                 &Domain);
        }
        ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
        ok_eq_pointer(Domain,
                      (PIOMMU_DMA_DOMAIN)(ULONG_PTR)0xA5A5A5A5);
    }

    RtlFillMemory(&Interface, sizeof(Interface), 0xA5);
    Status = IoGetIommuInterfaceEx(1, 1, &Interface);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_3);
    ok_eq_size(Interface.Size, (SIZE_T)0xA5A5A5A5A5A5A5A5ULL);
}
