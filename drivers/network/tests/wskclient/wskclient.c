/*
 * PROJECT:     ReactOS WSK client test driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * FILE:        drivers/network/tests/wskclient/wskclient.c
 * PURPOSE:     Minimal WSK client. Registers with netio.sys via WskRegister,
 *              captures the provider NPI, logs the result, and cleans up.
 *              Link-tests the WSK client registration path on the dev-nt6-1
 *              branch.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors.
 */

#include <ntifs.h>
#include <ws2def.h>
#include <wsk.h>

static WSK_REGISTRATION g_WskRegistration;
static WSK_CLIENT_NPI   g_ClientNpi;
static WSK_CLIENT_DISPATCH g_Dispatch = {
    MAKE_WSK_VERSION(1, 0),
    0,
    NULL
};

static VOID NTAPI
TestUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    WskDeregister(&g_WskRegistration);
}

NTSTATUS NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = TestUnload;

    g_ClientNpi.ClientContext = NULL;
    g_ClientNpi.Dispatch = &g_Dispatch;

    status = WskRegister(&g_ClientNpi, &g_WskRegistration);
    return status;
}

/* EOF */
