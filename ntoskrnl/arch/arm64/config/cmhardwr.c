/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/config/cmhardwr.c
 * PURPOSE:         Machine-dependent configuration stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    return STATUS_SUCCESS;
}
