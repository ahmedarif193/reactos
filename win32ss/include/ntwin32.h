
#pragma once

/* These are opaque for user mode */
typedef struct _PROCESSINFO *PPROCESSINFO;
typedef struct _THREADINFO *PTHREADINFO;

/*
 * W32CLIENTINFO: Prefix view of CLIENTINFO in TEB->Win32ClientInfo.
 *
 * Only the leading fields used by gdi32 are defined here. Do not mirror the
 * full CLIENTINFO layout in this header.
 */
typedef struct _W32CLIENTINFO
{
    ULONG_PTR CI_flags;
    ULONG_PTR cSpins;
} W32CLIENTINFO, *PW32CLIENTINFO;

/*
 * Slots in TEB->Win32ClientInfo[] for the fixed prefix fields.
 * Using slot writes avoids full-struct overlay/packing drift.
 */
#define W32CLIENTINFO_SLOT_CI_FLAGS 0
#define W32CLIENTINFO_SLOT_CSPINS   1

C_ASSERT(FIELD_OFFSET(W32CLIENTINFO, CI_flags) == 0);
C_ASSERT(FIELD_OFFSET(W32CLIENTINFO, cSpins) == sizeof(ULONG_PTR));

/*
 * NOTE: ntwin32.h is shared by kernel and user builds. Keep helper as a macro
 * so non-user translation units don't require NtCurrentTeb().
 */
#define W32SetClientSpins(_Spins) \
    (NtCurrentTeb()->Win32ClientInfo[W32CLIENTINFO_SLOT_CSPINS] = (ULONG_PTR)(_Spins))
