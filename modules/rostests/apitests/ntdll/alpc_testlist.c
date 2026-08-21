/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Standalone test list for native and ReactOS ALPC parity
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#define STANDALONE
#include <apitest.h>

extern void func_AlpcCompletionList(void);
extern void func_AlpcMessageAttribute(void);
extern void func_NtAlpcConnectMatrix(void);
extern void func_NtAlpcInformation(void);
extern void func_NtAlpcLifecycle(void);
extern void func_NtAlpcMessageAttributes(void);
extern void func_NtAlpcMessageBoundary(void);
extern void func_NtAlpcPort(void);
extern void func_NtAlpcResources(void);
extern void func_NtAlpcValidation(void);
extern void func_TpAlpc(void);

const struct test winetest_testlist[] =
{
    { "AlpcCompletionList",       func_AlpcCompletionList },
    { "AlpcMessageAttribute",     func_AlpcMessageAttribute },
    { "NtAlpcConnectMatrix",      func_NtAlpcConnectMatrix },
    { "NtAlpcInformation",        func_NtAlpcInformation },
    { "NtAlpcLifecycle",          func_NtAlpcLifecycle },
    { "NtAlpcMessageAttributes",  func_NtAlpcMessageAttributes },
    { "NtAlpcMessageBoundary",    func_NtAlpcMessageBoundary },
    { "NtAlpcPort",               func_NtAlpcPort },
    { "NtAlpcResources",          func_NtAlpcResources },
    { "NtAlpcValidation",         func_NtAlpcValidation },
    { "TpAlpc",                   func_TpAlpc },
    { 0, 0 }
};
