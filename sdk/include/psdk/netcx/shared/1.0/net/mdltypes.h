/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx shared data path definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#pragma region Desktop Family or OneCore Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820)

typedef struct _NET_FRAGMENT_MDL
{

    MDL *
        Mdl;

} NET_FRAGMENT_MDL;

C_ASSERT(sizeof(NET_FRAGMENT_MDL) == sizeof(void *));

#pragma warning(pop)

EXTERN_C_END

#define NET_FRAGMENT_EXTENSION_MDL_NAME L"ms_fragment_mdl"
#define NET_FRAGMENT_EXTENSION_MDL_VERSION_1 1U

#endif
#pragma endregion
