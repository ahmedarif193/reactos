/*
 * PROJECT:     ReactOS UEFI Support
 * LICENSE:     CC-BY-4.0
 * PURPOSE:     RISC-V EFI Boot Protocol definitions
 */

#pragma once

#include <Uefi.h>

#define RISCV_EFI_BOOT_PROTOCOL_GUID \
    { 0xccd15fec, 0x6f73, 0x4eec, \
      { 0x83, 0x95, 0x3e, 0x69, 0xe4, 0xb9, 0x40, 0xbf } }

#define RISCV_EFI_BOOT_PROTOCOL_REVISION       0x00010000ULL
#define RISCV_EFI_BOOT_PROTOCOL_LATEST_VERSION RISCV_EFI_BOOT_PROTOCOL_REVISION

typedef struct _RISCV_EFI_BOOT_PROTOCOL RISCV_EFI_BOOT_PROTOCOL;

typedef EFI_STATUS
(EFIAPI *EFI_GET_BOOT_HARTID)(
    _In_ RISCV_EFI_BOOT_PROTOCOL *This,
    _Out_ UINTN *BootHartId);

struct _RISCV_EFI_BOOT_PROTOCOL
{
    UINT64 Revision;
    EFI_GET_BOOT_HARTID GetBootHartId;
};
