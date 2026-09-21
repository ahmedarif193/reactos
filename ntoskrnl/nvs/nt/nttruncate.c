/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/nttruncate.c
 * PURPOSE:     NT file truncation eligibility
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/cc/host/ccntshim.h>
#else
#include <nvs/nt/mint.h>
#endif

BOOLEAN
NTAPI
MmCanFileBeTruncated(
    _In_ PSECTION_OBJECT_POINTERS SectionPointer,
    _In_opt_ PLARGE_INTEGER NewFileSize)
{
    PMI_CONTROL_AREA Control;
    BOOLEAN Allowed = TRUE;

    if (SectionPointer->ImageSectionObject != NULL && !MmFlushImageSection(SectionPointer, MmFlushForWrite))
        return FALSE;

    Control = MiReferenceDataControlArea(SectionPointer);
    if (Control == NULL)
        return TRUE;

    if (MI_ATOMIC_READ32(&Control->Segment->TruncationViews) != 0)
    {
        ULONG64 NewSize = (NewFileSize != NULL) ? (ULONG64)NewFileSize->QuadPart : 0;

        if (NewSize < (ULONG64)MI_ATOMIC_READ64(&Control->Segment->SizeInBytes))
            Allowed = FALSE;
    }

    if (Allowed && NewFileSize != NULL)
        MiSegmentPurge(Control->Segment, (ULONG64)NewFileSize->QuadPart, 0);

    MiDereferenceControlArea(Control);
    return Allowed;
}

