/*
 * Copyright (C) 2025 Louis Lenders
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _POWERSETTING_H_
#define _POWERSETTING_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _HPOWERNOTIFY_DEF_
#define _HPOWERNOTIFY_DEF_

typedef PVOID HPOWERNOTIFY, *PHPOWERNOTIFY;

#endif

typedef enum EFFECTIVE_POWER_MODE
{
    EffectivePowerModeBatterySaver,
    EffectivePowerModeBetterBattery,
    EffectivePowerModeBalanced,
    EffectivePowerModeHighPerformance,
    EffectivePowerModeMaxPerformance,
    EffectivePowerModeGameMode,
    EffectivePowerModeMixedReality,
} EFFECTIVE_POWER_MODE;

typedef void WINAPI EFFECTIVE_POWER_MODE_CALLBACK(EFFECTIVE_POWER_MODE mode, void *context);

#if (NTDDI_VERSION >= NTDDI_VISTA)
DWORD WINAPI PowerWriteACValueIndex(_In_opt_ HKEY, _In_ const GUID *, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_ DWORD);
DWORD WINAPI PowerWriteDCValueIndex(_In_opt_ HKEY, _In_ const GUID *, _In_opt_ const GUID *, _In_opt_ const GUID *, _In_ DWORD);
DWORD WINAPI PowerGetActiveScheme(_In_opt_ HKEY, _Outptr_ GUID **);
DWORD WINAPI PowerSetActiveScheme(_In_opt_ HKEY, _In_opt_ const GUID *);
#endif

HRESULT WINAPI PowerRegisterForEffectivePowerModeNotifications(ULONG, EFFECTIVE_POWER_MODE_CALLBACK*, void*, void**);

#ifdef __cplusplus
}
#endif

#endif /* _POWERSETTING_H_ */
