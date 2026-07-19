/*
 * PROJECT:     ReactOS HID over I2C minidriver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HID-I2C transfer completion validation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "hidi2c.h"

NTSTATUS
Hidi2cValidateTransferResult(
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information,
    _In_ ULONG_PTR ExpectedLength)
{
    if (Status != STATUS_SUCCESS)
        return Status;

    return Information == ExpectedLength ?
           STATUS_SUCCESS : STATUS_DEVICE_PROTOCOL_ERROR;
}
