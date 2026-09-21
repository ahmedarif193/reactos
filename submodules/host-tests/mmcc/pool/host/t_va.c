/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/host/t_va.c
 * PURPOSE:     Pool virtual address host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "harness.h"

void
TestVa(void)
{
    TEST_ARENA Arena;
    POOL_VA_REGION Region;
    PVOID Run[8];
    PVOID Meta;
    SIZE_T Baseline;
    SIZE_T Free;
    ULONG i;

    ArenaCreate(&Arena, 64 * POOL_UNIT_SIZE);
    CHECK(PoolVaRegionInitialize(&Region, Arena.Base, Arena.Bytes, FALSE, &Arena.Backing) == STATUS_SUCCESS);
    CHECK((Region.Base & POOL_UNIT_MASK) == 0);
    CHECK(Region.UnitCount >= 63);
    CHECK(Region.Directory[0].State == PoolUnitMetadata);
    Baseline = Region.UnitsInUse;

    Run[0] = PoolVaReserve(&Region, 1, PoolUnitSegment);
    Run[1] = PoolVaReserve(&Region, 4, PoolUnitLarge);
    Run[2] = PoolVaReserve(&Region, 1, PoolUnitSegment);
    CHECK(Run[0] != NULL && Run[1] != NULL && Run[2] != NULL);
    CHECK(PoolVaUnit(&Region, Run[0])->State == PoolUnitSegment);
    CHECK(PoolVaUnit(&Region, Run[1])->State == PoolUnitLarge);
    CHECK(PoolVaUnit(&Region, Run[1])->UnitCount == 4);
    CHECK(PoolVaUnit(&Region, (PUCHAR)Run[1] + POOL_UNIT_SIZE)->State == PoolUnitTail);
    CHECK(PoolVaUnit(&Region, (PUCHAR)Run[1] + 3 * POOL_UNIT_SIZE + 123)->State == PoolUnitTail);
    CHECK(Region.UnitsInUse == Baseline + 6);

    PoolVaRelease(&Region, Run[1], 4);
    CHECK(PoolVaUnit(&Region, Run[1])->State == PoolUnitFree);
    Run[3] = PoolVaReserve(&Region, 3, PoolUnitLarge);
    CHECK(Run[3] == Run[1]);
    PoolVaRelease(&Region, Run[3], 3);
    PoolVaRelease(&Region, Run[0], 1);
    PoolVaRelease(&Region, Run[2], 1);
    CHECK(Region.UnitsInUse == Baseline);

    Free = Region.UnitCount - Region.UnitsInUse;
    CHECK(PoolVaReserve(&Region, Free + 1, PoolUnitLarge) == NULL);
    Run[4] = PoolVaReserve(&Region, Free, PoolUnitLarge);
    CHECK(Run[4] != NULL);
    CHECK(PoolVaReserve(&Region, 1, PoolUnitSegment) == NULL);
    PoolVaRelease(&Region, Run[4], Free);

    for (i = 0; i < 8; i++)
        Run[i] = PoolVaReserve(&Region, 2, PoolUnitLarge);
    for (i = 0; i < 8; i += 2)
        PoolVaRelease(&Region, Run[i], 2);
    CHECK(PoolVaReserve(&Region, 2, PoolUnitLarge) != NULL);
    for (i = 1; i < 8; i += 2)
        PoolVaRelease(&Region, Run[i], 2);

    Meta = PoolVaAllocateMetadata(&Region, 100);
    CHECK(Meta != NULL && ((ULONG_PTR)Meta % POOL_CACHE_ALIGNMENT) == 0);
    memset(Meta, 0x11, 100);
    Meta = PoolVaAllocateMetadata(&Region, 3 * POOL_UNIT_SIZE / 2);
    CHECK(Meta != NULL);
    memset(Meta, 0x22, 3 * POOL_UNIT_SIZE / 2);
    CHECK(PoolVaUnit(&Region, Meta)->State == PoolUnitMetadata ||
          PoolVaUnit(&Region, Meta)->State == PoolUnitTail);

    ArenaFailAfter(&Arena, 0);
    CHECK(PoolVaAllocateMetadata(&Region, 4 * POOL_UNIT_SIZE) == NULL);
    ArenaFailAfter(&Arena, -1);

    CHECK(Region.CommittedPages == Arena.CommittedPages);
    CHECK(Arena.Errors == 0);
    ArenaDestroy(&Arena);
}
